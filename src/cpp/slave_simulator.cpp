#include "cse/slave_simulator.hpp"

#include <boost/container/vector.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>


namespace cse
{
namespace
{

template<typename T>
struct var_view_type
{
    using type = T;
};

template<>
struct var_view_type<std::string>
{
    using type = std::string_view;
};


template<typename T>
struct get_variable_cache
{
    std::vector<variable_index> indexes;
    boost::container::vector<T> originalValues;
    boost::container::vector<T> manipulatedValues;
    std::vector<std::function<T(T)>> manipulators;
    std::unordered_map<variable_index, std::size_t> indexMapping;

    void expose(variable_index i)
    {
        if (indexMapping.count(i)) return;
        indexes.push_back(i);
        originalValues.push_back(T()); // TODO: Use start value from model description
        manipulatedValues.push_back(T());
        manipulators.emplace_back();
        indexMapping[i] = indexes.size() - 1;
    }

    typename var_view_type<T>::type get(variable_index i) const
    {
        const auto it = indexMapping.find(i);
        if (it != indexMapping.end()) {
            return manipulatedValues[it->second];
        } else {
            std::ostringstream oss;
            oss << "variable_index " << i
                << " not found in exposed variables. Variables must be exposed before calling get()";
            throw std::out_of_range(oss.str());
        }
    }

    void set_manipulator(variable_index i, std::function<T(T)> m)
    {
        manipulators[indexMapping[i]] = m;
    }

    void run_manipulators()
    {
        for (std::size_t i = 0; i < originalValues.size(); ++i) {
            if (manipulators[i]) {
                manipulatedValues[i] = manipulators[i](originalValues[i]);
            } else {
                manipulatedValues[i] = originalValues[i];
            }
        }
    }
};


template<typename T>
class set_variable_cache
{
public:
    void expose(variable_index i)
    {
        // TODO: Here, exposed_variable::lastValue will be initialised with
        // the value T().  We ought to use the start value from the model
        // description instead.
        exposedVariables_.emplace(i, exposed_variable());
    }

    void set_value(variable_index i, typename var_view_type<T>::type v)
    {
        assert(!hasRunManipulators_);
        const auto it = exposedVariables_.find(i);
        if (it == exposedVariables_.end()) {
            std::ostringstream oss;
            oss << "variable_index " << i
                << " not found in exposed variables. Variables must be exposed before calling set()";
            throw std::out_of_range(oss.str());
        }
        it->second.lastValue = v;
        if (it->second.arrayIndex < 0) {
            it->second.arrayIndex = indexes_.size();
            assert(indexes_.size() == values_.size());
            indexes_.emplace_back(i);
            values_.emplace_back(v);
        } else {
            assert(indexes_[it->second.arrayIndex] == i);
            values_[it->second.arrayIndex] = v;
        }
    }

    void set_manipulator(variable_index i, std::function<T(T)> m)
    {
        assert(!hasRunManipulators_);
        const auto it = exposedVariables_.find(i);
        if (it == exposedVariables_.end()) {
            std::ostringstream oss;
            oss << "variable_index " << i
                << " not found in exposed variables. Variables must be exposed before calling set_manipulator()";
            throw std::out_of_range(oss.str());
        }
        if (it->second.arrayIndex < 0) {
            // Ensure that the simulator receives an updated value.
            it->second.arrayIndex = indexes_.size();
            assert(indexes_.size() == values_.size());
            indexes_.emplace_back(i);
            values_.emplace_back();
        }
        if (m) {
            manipulators_[i] = m;
        } else {
            manipulators_.erase(i);
        }
    }

    std::pair<gsl::span<variable_index>, gsl::span<const T>> manipulate_and_get()
    {
        if (!hasRunManipulators_) {
            assert(indexes_.size() == values_.size());
            for (std::size_t i = 0; i < indexes_.size(); ++i) {
                const auto iterator = manipulators_.find(indexes_[i]);
                if (iterator != manipulators_.end()) {
                    values_[i] = iterator->second(values_[i]);
                }
            }
            hasRunManipulators_ = true;
        }
        return std::pair(gsl::make_span(indexes_), gsl::make_span(values_));
    }

    void reset()
    {
        for (auto index : indexes_) {
            exposedVariables_.at(index).arrayIndex = -1;
        }
        indexes_.clear();
        values_.clear();
        hasRunManipulators_ = false;
    }

private:
    struct exposed_variable
    {
        // The last set value of the variable.
        T lastValue = T();

        // The variable's index in the `indexes_` and `values_` arrays, or
        // -1 if it hasn't been added to them yet.
        std::ptrdiff_t arrayIndex = -1;
    };

    std::unordered_map<variable_index, exposed_variable> exposedVariables_;

    // The manipulators associated with certain variables, and a flag that
    // specifies whether they have been run on the values currently in
    // `values_`.
    std::unordered_map<variable_index, std::function<T(T)>> manipulators_;
    bool hasRunManipulators_ = false;

    // The indexes and values of the variables that will be set next.
    std::vector<variable_index> indexes_;
    boost::container::vector<T> values_;
};


// Copies the contents of a contiguous-storage container or span into another.
template<typename Src, typename Tgt>
void copy_contents(Src&& src, Tgt&& tgt)
{
    assert(static_cast<std::size_t>(src.size()) <= static_cast<std::size_t>(tgt.size()));
    std::copy(src.data(), src.data() + src.size(), tgt.data());
}
} // namespace


class slave_simulator::impl
{
public:
    impl(std::shared_ptr<async_slave> slave, std::string_view name)
        : slave_(std::move(slave))
        , name_(name)
        , modelDescription_(slave_->model_description().get())
    {
        assert(slave_);
        assert(!name_.empty());
    }

    ~impl() noexcept = default;

    impl(const impl&) = delete;

    impl& operator=(const impl&) = delete;

    impl(impl&&) noexcept = delete;

    impl& operator=(impl&&) noexcept = delete;

    std::string name() const
    {
        return name_;
    }

    cse::model_description model_description() const
    {
        return modelDescription_;
    }


    void expose_for_getting(variable_type type, variable_index index)
    {
        switch (type) {
            case variable_type::real:
                realGetCache_.expose(index);
                break;
            case variable_type::integer:
                integerGetCache_.expose(index);
                break;
            case variable_type::boolean:
                booleanGetCache_.expose(index);
                break;
            case variable_type::string:
                stringGetCache_.expose(index);
                break;
        }
    }

    double get_real(variable_index index) const
    {
        return realGetCache_.get(index);
    }

    int get_integer(variable_index index) const
    {
        return integerGetCache_.get(index);
    }

    bool get_boolean(variable_index index) const
    {
        return booleanGetCache_.get(index);
    }

    std::string_view get_string(variable_index index) const
    {
        return stringGetCache_.get(index);
    }

    void expose_for_setting(variable_type type, variable_index index)
    {
        switch (type) {
            case variable_type::real:
                realSetCache_.expose(index);
                break;
            case variable_type::integer:
                integerSetCache_.expose(index);
                break;
            case variable_type::boolean:
                booleanSetCache_.expose(index);
                break;
            case variable_type::string:
                stringSetCache_.expose(index);
                break;
        }
    }

    void set_real(variable_index index, double value)
    {
        realSetCache_.set_value(index, value);
    }

    void set_integer(variable_index index, int value)
    {
        integerSetCache_.set_value(index, value);
    }

    void set_boolean(variable_index index, bool value)
    {
        booleanSetCache_.set_value(index, value);
    }

    void set_string(variable_index index, std::string_view value)
    {
        stringSetCache_.set_value(index, value);
    }

    void set_real_input_manipulator(
        variable_index index,
        std::function<double(double)> manipulator)
    {
        realSetCache_.set_manipulator(index, manipulator);
    }

    void set_integer_input_manipulator(
        variable_index index,
        std::function<int(int)> manipulator)
    {
        integerSetCache_.set_manipulator(index, manipulator);
    }

    void set_boolean_input_manipulator(
        variable_index index,
        std::function<bool(bool)> manipulator)
    {
        booleanSetCache_.set_manipulator(index, manipulator);
    }

    void set_string_input_manipulator(
        variable_index index,
        std::function<std::string(std::string_view)> manipulator)
    {
        stringSetCache_.set_manipulator(index, manipulator);
    }

    void set_real_output_manipulator(
        variable_index index,
        std::function<double(double)> manipulator)
    {
        realGetCache_.set_manipulator(index, manipulator);
    }

    void set_integer_output_manipulator(
        variable_index index,
        std::function<int(int)> manipulator)
    {
        integerGetCache_.set_manipulator(index, manipulator);
    }

    void set_boolean_output_manipulator(
        variable_index index,
        std::function<bool(bool)> manipulator)
    {
        booleanGetCache_.set_manipulator(index, manipulator);
    }

    void set_string_output_manipulator(
        variable_index index,
        std::function<std::string(std::string_view)> manipulator)
    {
        stringGetCache_.set_manipulator(index, manipulator);
    }

    boost::fibers::future<void> setup(
        time_point startTime,
        std::optional<time_point> stopTime,
        std::optional<double> relativeTolerance)
    {
        return slave_->setup(startTime, stopTime, relativeTolerance);
    }

    boost::fibers::future<void> do_iteration()
    {
        // clang-format off
            return boost::fibers::async([=]() {
                set_variables();
                get_variables();
            });
        // clang-format on
    }

    boost::fibers::future<step_result> do_step(
        time_point currentT,
        duration deltaT)
    {
        // clang-format off
            return boost::fibers::async([=]() {
                if (slave_->state() == slave_state::initialisation) {
                    slave_->start_simulation().get();
                }
                set_variables();
                const auto result = slave_->do_step(currentT, deltaT).get();
                get_variables();
                return result;
            });
        // clang-format on
    }

private:
    void set_variables()
    {
        const auto [realIndexes, realValues] = realSetCache_.manipulate_and_get();
        const auto [integerIndexes, integerValues] = integerSetCache_.manipulate_and_get();
        const auto [booleanIndexes, booleanValues] = booleanSetCache_.manipulate_and_get();
        const auto [stringIndexes, stringValues] = stringSetCache_.manipulate_and_get();
        slave_->set_variables(
                  gsl::make_span(realIndexes),
                  gsl::make_span(realValues),
                  gsl::make_span(integerIndexes),
                  gsl::make_span(integerValues),
                  gsl::make_span(booleanIndexes),
                  gsl::make_span(booleanValues),
                  gsl::make_span(stringIndexes),
                  gsl::make_span(stringValues))
            .get();
        realSetCache_.reset();
        integerSetCache_.reset();
        booleanSetCache_.reset();
        stringSetCache_.reset();
    }

    void get_variables()
    {
        const auto values = slave_->get_variables(
                                      gsl::make_span(realGetCache_.indexes),
                                      gsl::make_span(integerGetCache_.indexes),
                                      gsl::make_span(booleanGetCache_.indexes),
                                      gsl::make_span(stringGetCache_.indexes))
                                .get();
        copy_contents(values.real, realGetCache_.originalValues);
        copy_contents(values.integer, integerGetCache_.originalValues);
        copy_contents(values.boolean, booleanGetCache_.originalValues);
        copy_contents(values.string, stringGetCache_.originalValues);
        realGetCache_.run_manipulators();
        integerGetCache_.run_manipulators();
        booleanGetCache_.run_manipulators();
        stringGetCache_.run_manipulators();
    }

private:
    std::shared_ptr<async_slave> slave_;
    std::string name_;
    cse::model_description modelDescription_;

    get_variable_cache<double> realGetCache_;
    get_variable_cache<int> integerGetCache_;
    get_variable_cache<bool> booleanGetCache_;
    get_variable_cache<std::string> stringGetCache_;

    set_variable_cache<double> realSetCache_;
    set_variable_cache<int> integerSetCache_;
    set_variable_cache<bool> booleanSetCache_;
    set_variable_cache<std::string> stringSetCache_;
};


slave_simulator::slave_simulator(
    std::shared_ptr<async_slave> slave,
    std::string_view name)
    : pimpl_(std::make_unique<impl>(std::move(slave), name))
{
}


slave_simulator::~slave_simulator() noexcept = default;

slave_simulator::slave_simulator(slave_simulator&&) noexcept = default;

slave_simulator& slave_simulator::operator=(slave_simulator&&) noexcept = default;


std::string slave_simulator::name() const
{
    return pimpl_->name();
}


cse::model_description slave_simulator::model_description() const
{
    return pimpl_->model_description();
}


void slave_simulator::expose_for_getting(variable_type type, variable_index index)
{
    pimpl_->expose_for_getting(type, index);
}


double slave_simulator::get_real(variable_index index) const
{
    return pimpl_->get_real(index);
}


int slave_simulator::get_integer(variable_index index) const
{
    return pimpl_->get_integer(index);
}


bool slave_simulator::get_boolean(variable_index index) const
{
    return pimpl_->get_boolean(index);
}


std::string_view slave_simulator::get_string(variable_index index) const
{
    return pimpl_->get_string(index);
}


void slave_simulator::expose_for_setting(variable_type type, variable_index index)
{
    pimpl_->expose_for_setting(type, index);
}


void slave_simulator::set_real(variable_index index, double value)
{
    pimpl_->set_real(index, value);
}


void slave_simulator::set_integer(variable_index index, int value)
{
    pimpl_->set_integer(index, value);
}


void slave_simulator::set_boolean(variable_index index, bool value)
{
    pimpl_->set_boolean(index, value);
}


void slave_simulator::set_string(variable_index index, std::string_view value)
{
    pimpl_->set_string(index, value);
}

void slave_simulator::set_real_input_manipulator(
    variable_index index,
    std::function<double(double)> manipulator)
{
    pimpl_->set_real_input_manipulator(index, manipulator);
}

void slave_simulator::set_integer_input_manipulator(
    variable_index index,
    std::function<int(int)> manipulator)
{
    pimpl_->set_integer_input_manipulator(index, manipulator);
}

void slave_simulator::set_boolean_input_manipulator(
    variable_index index,
    std::function<bool(bool)> manipulator)
{
    pimpl_->set_boolean_input_manipulator(index, manipulator);
}

void slave_simulator::set_string_input_manipulator(
    variable_index index,
    std::function<std::string(std::string_view)> manipulator)
{
    pimpl_->set_string_input_manipulator(index, manipulator);
}

void slave_simulator::set_real_output_manipulator(
    variable_index index,
    std::function<double(double)> manipulator)
{
    pimpl_->set_real_output_manipulator(index, manipulator);
}

void slave_simulator::set_integer_output_manipulator(
    variable_index index,
    std::function<int(int)> manipulator)
{
    pimpl_->set_integer_output_manipulator(index, manipulator);
}

void slave_simulator::set_boolean_output_manipulator(
    variable_index index,
    std::function<bool(bool)> manipulator)
{
    pimpl_->set_boolean_output_manipulator(index, manipulator);
}

void slave_simulator::set_string_output_manipulator(
    variable_index index,
    std::function<std::string(std::string_view)> manipulator)
{
    pimpl_->set_string_output_manipulator(index, manipulator);
}

boost::fibers::future<void> slave_simulator::setup(
    time_point startTime,
    std::optional<time_point> stopTime,
    std::optional<double> relativeTolerance)
{
    return pimpl_->setup(startTime, stopTime, relativeTolerance);
}


boost::fibers::future<void> slave_simulator::do_iteration()
{
    return pimpl_->do_iteration();
}


boost::fibers::future<step_result> slave_simulator::do_step(
    time_point currentT,
    duration deltaT)
{
    return pimpl_->do_step(currentT, deltaT);
}


} // namespace cse
