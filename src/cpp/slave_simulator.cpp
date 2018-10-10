#include "cse/slave_simulator.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <unordered_map>

#include <boost/container/vector.hpp>


namespace cse
{
namespace
{

template<typename T> struct var_view_type { using type = T; };
template<> struct var_view_type<std::string> { using type = std::string_view; };

template<typename T>
struct exposed_vars
{
    std::vector<variable_index> indexes;
    boost::container::vector<T> values;
    std::unordered_map<variable_index, std::size_t> indexMapping;

    void expose(variable_index i)
    {
        indexes.push_back(i);
        values.push_back(T()); // TODO: Use start value from model description
        indexMapping[i] = indexes.size() - 1;
    }

    typename var_view_type<T>::type get(variable_index i) const
    {
        return values[indexMapping.at(i)];
    }

    void set(variable_index i, typename var_view_type<T>::type v)
    {
        values[indexMapping.at(i)] = v;
    }
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
    impl(std::unique_ptr<async_slave> slave, std::string_view name)
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


    void expose_output(variable_type type, variable_index index)
    {
        switch (type) {
            case variable_type::real:
                realOutputs_.expose(index);
                break;
            case variable_type::integer:
                integerOutputs_.expose(index);
                break;
            case variable_type::boolean:
                booleanOutputs_.expose(index);
                break;
            case variable_type::string:
                stringOutputs_.expose(index);
                break;
        }
    }

    double get_real(variable_index index) const
    {
        return realOutputs_.get(index);
    }

    int get_integer(variable_index index) const
    {
        return integerOutputs_.get(index);
    }

    bool get_boolean(variable_index index) const
    {
        return booleanOutputs_.get(index);
    }

    std::string_view get_string(variable_index index) const
    {
        return stringOutputs_.get(index);
    }

    void expose_input(variable_type type, variable_index index)
    {
        switch (type) {
            case variable_type::real:
                realInputs_.expose(index);
                break;
            case variable_type::integer:
                integerInputs_.expose(index);
                break;
            case variable_type::boolean:
                booleanInputs_.expose(index);
                break;
            case variable_type::string:
                stringInputs_.expose(index);
                break;
        }
    }

    void set_real(variable_index index, double value)
    {
        realInputs_.set(index, value);
    }

    void set_integer(variable_index index, int value)
    {
        integerInputs_.set(index, value);
    }

    void set_boolean(variable_index index, bool value)
    {
        booleanInputs_.set(index, value);
    }

    void set_string(variable_index index, std::string_view value)
    {
        stringInputs_.set(index, value);
    }

    boost::fibers::future<void> setup(
        time_point startTime,
        std::optional<time_point> stopTime,
        std::optional<double> tolerance)
    {
        return slave_->setup(
            name_,
            std::string(),
            startTime,
            stopTime ? *stopTime : cse::eternity,
            !!tolerance,
            tolerance ? *tolerance : 0.0);
    }

    boost::fibers::future<step_result> do_step(
        time_point currentT,
        time_duration deltaT)
    {
        // clang-format off
        return boost::fibers::async([=]() {
            slave_->set_variables(
                    gsl::make_span(realInputs_.indexes),
                    gsl::make_span(realInputs_.values),
                    gsl::make_span(integerInputs_.indexes),
                    gsl::make_span(integerInputs_.values),
                    gsl::make_span(booleanInputs_.indexes),
                    gsl::make_span(booleanInputs_.values),
                    gsl::make_span(stringInputs_.indexes),
                    gsl::make_span(stringInputs_.values)
                ).get();
            const auto result = slave_->do_step(currentT, deltaT).get();
            const auto values = slave_->get_variables(
                    gsl::make_span(realOutputs_.indexes),
                    gsl::make_span(integerOutputs_.indexes),
                    gsl::make_span(booleanOutputs_.indexes),
                    gsl::make_span(stringOutputs_.indexes)
                ).get();
            copy_contents(values.real,    realOutputs_.values);
            copy_contents(values.integer, integerOutputs_.values);
            copy_contents(values.boolean, booleanOutputs_.values);
            copy_contents(values.string,  stringOutputs_.values);
            return result;
        });
        // clang-format on
    }

private:
    std::unique_ptr<async_slave> slave_;
    std::string name_;
    cse::model_description modelDescription_;

    exposed_vars<double> realOutputs_;
    exposed_vars<int> integerOutputs_;
    exposed_vars<bool> booleanOutputs_;
    exposed_vars<std::string> stringOutputs_;

    exposed_vars<double> realInputs_;
    exposed_vars<int> integerInputs_;
    exposed_vars<bool> booleanInputs_;
    exposed_vars<std::string> stringInputs_;
};


slave_simulator::slave_simulator(
    std::unique_ptr<async_slave> slave,
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


void slave_simulator::expose_output(variable_type type, variable_index index)
{
    pimpl_->expose_output(type, index);
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


void slave_simulator::expose_input(variable_type type, variable_index index)
{
    pimpl_->expose_input(type, index);
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


boost::fibers::future<void> slave_simulator::setup(
    time_point startTime,
    std::optional<time_point> stopTime,
    std::optional<double> tolerance)
{
    return pimpl_->setup(startTime, stopTime, tolerance);
}


boost::fibers::future<step_result> slave_simulator::do_step(
    time_point currentT,
    time_duration deltaT)
{
    return pimpl_->do_step(currentT, deltaT);
}


} // namespace cse
