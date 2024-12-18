/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/slave_simulator.hpp"

#include "cosim/error.hpp"
#include <cosim/utility/utility.hpp>

#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <unordered_map>


namespace cosim
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

/**
 *  Helper class which checks, sets and resets the state variable for
 *  an `slave_simulator`.
 *
 *  The constructors of this class take a reference to the `slave_state`
 *  variable to be managed, and immediately set it to `indeterminate`.
 *  On destruction, the managed variable will be automatically set to
 *  a specified value, or, if an exception is currently "in flight", to
 *  the special `error` value.
 */
class state_guard
{
public:
    /**
     *  Constructs a `state_guard` that sets `stateVariable` to `finalState`
     *  on destruction.
     */
    state_guard(slave_state& stateVariable, slave_state finalState)
        : stateVariable_(&stateVariable)
        , finalState_(finalState)
    {
        stateVariable = slave_state::indeterminate;
    }

    /**
     *  Constructs a `state_guard` that resets `stateVariable` to its original
     *  value on destruction.
     */
    state_guard(slave_state& stateVariable)
        : state_guard(stateVariable, stateVariable)
    { }

    /**
     *  Manually sets the managed variable to its final value and relinquishes
     *  control of it.  Does not check for exceptions.
     */
    void reset() noexcept
    {
        if (stateVariable_ != nullptr) {
            *stateVariable_ = finalState_;
            stateVariable_ = nullptr;
        }
    }

    ~state_guard() noexcept
    {
        if (stateVariable_ != nullptr) {
            if (std::uncaught_exceptions()) {
                *stateVariable_ = slave_state::error;
            } else {
                *stateVariable_ = finalState_;
            }
        }
    }

    // Disallow copying, so that we don't inadvertently end up in a situation
    // where multiple `state_guard` objects try to manage the same state.
    state_guard(const state_guard&) = delete;
    state_guard& operator=(const state_guard&) = delete;

    state_guard(state_guard&& other) noexcept
        : stateVariable_(other.stateVariable_)
        , finalState_(other.finalState_)
    {
        other.stateVariable_ = nullptr;
    }
    state_guard& operator=(state_guard&& other) noexcept
    {
        stateVariable_ = other.stateVariable_;
        finalState_ = other.finalState_;
        other.stateVariable_ = nullptr;
        return *this;
    }

private:
    slave_state* stateVariable_;
    slave_state finalState_;
};


template<typename T>
struct get_variable_cache
{
    std::vector<value_reference> references;
    boost::container::vector<T> originalValues;
    boost::container::vector<T> modifiedValues;
    std::vector<std::function<T(T, duration)>> modifiers;
    std::unordered_map<value_reference, std::size_t> indexMapping;

    void expose(value_reference r)
    {
        if (indexMapping.count(r)) return;
        references.push_back(r);
        originalValues.push_back(T()); // TODO: Use start value from model description
        modifiedValues.push_back(T());
        modifiers.emplace_back();
        indexMapping[r] = references.size() - 1;
    }

    typename var_view_type<T>::type get(value_reference r) const
    {
        const auto it = indexMapping.find(r);
        if (it != indexMapping.end()) {
            return modifiedValues[it->second];
        } else {
            std::ostringstream oss;
            oss << "Variable with reference " << r
                << " not found in exposed variables. Variables must be exposed before calling get()";
            throw std::out_of_range(oss.str());
        }
    }

    void set_modifier(value_reference r, std::function<T(T, duration)> m)
    {
        modifiers[indexMapping[r]] = m;
    }

    void run_modifiers(duration deltaT)
    {
        for (std::size_t i = 0; i < originalValues.size(); ++i) {
            if (modifiers[i]) {
                modifiedValues[i] = modifiers[i](originalValues[i], deltaT);
            } else {
                modifiedValues[i] = originalValues[i];
            }
        }
    }
};


template<typename T>
class set_variable_cache
{
public:
    void expose(value_reference r, T startValue)
    {
        exposedVariables_.emplace(r, exposed_variable{startValue, -1});
    }

    void set_value(value_reference r, typename var_view_type<T>::type v)
    {
        assert(!hasRunModifiers_);
        const auto it = exposedVariables_.find(r);
        if (it == exposedVariables_.end()) {
            std::ostringstream oss;
            oss << "Variable with value reference " << r
                << " not found in exposed variables. Variables must be exposed before calling set_value()";
            throw std::out_of_range(oss.str());
        }
        it->second.lastValue = v;
        if (it->second.arrayIndex < 0) {
            it->second.arrayIndex = references_.size();
            assert(references_.size() == values_.size());
            references_.emplace_back(r);
            values_.emplace_back(v);
        } else {
            assert(references_[it->second.arrayIndex] == r);
            values_[it->second.arrayIndex] = v;
        }
    }

    void set_modifier(value_reference r, std::function<T(T, duration)> m)
    {
        assert(!hasRunModifiers_);
        const auto it = exposedVariables_.find(r);
        if (it == exposedVariables_.end()) {
            std::ostringstream oss;
            oss << "Variable with value reference " << r
                << " not found in exposed variables. Variables must be exposed before calling set_modifier()";
            throw std::out_of_range(oss.str());
        }
        if (it->second.arrayIndex < 0) {
            // Ensure that the simulator receives an updated value.
            it->second.arrayIndex = references_.size();
            assert(references_.size() == values_.size());
            references_.emplace_back(r);
            values_.emplace_back(it->second.lastValue);
        }
        if (m) {
            modifiers_[r] = m;
        } else {
            modifiers_.erase(r);
        }
    }

    std::pair<gsl::span<value_reference>, gsl::span<const T>> modify_and_get(
        duration deltaT,
        std::optional<std::function<bool(const value_reference, const T&)>> filter = std::nullopt)
    {
        if (!hasRunModifiers_) {
            for (const auto& entry : modifiers_) {
                const auto ref = entry.first;
                auto& arrayIndex = exposedVariables_.at(ref).arrayIndex;
                if (arrayIndex < 0) {
                    arrayIndex = references_.size();
                    assert(references_.size() == values_.size());
                    references_.emplace_back(ref);
                    values_.emplace_back(exposedVariables_.at(ref).lastValue);
                }
                values_[arrayIndex] = entry.second(values_[arrayIndex], deltaT);
            }
            assert(references_.size() == values_.size());
            hasRunModifiers_ = true;
        }

        if (filter) {
            references_filtered_.clear();
            values_filtered_.clear();

            for (size_t i = 0; i < references_.size(); i++) {
                auto& ref = references_.at(i);
                auto& value = values_.at(i);

                if ((*filter)(ref, value)) {
                    references_filtered_.push_back(ref);
                    values_filtered_.push_back(value);
                }
            }

            return std::pair(gsl::make_span(references_filtered_), gsl::make_span(values_filtered_));
        }

        return std::pair(gsl::make_span(references_), gsl::make_span(values_));
    }

    void reset()
    {
        for (auto ref : references_) {
            exposedVariables_.at(ref).arrayIndex = -1;
        }
        references_.clear();
        values_.clear();
        references_filtered_.clear();
        values_filtered_.clear();
        hasRunModifiers_ = false;
    }

private:
    struct exposed_variable
    {
        // The last set value of the variable.
        T lastValue = T();

        // The variable's reference in the `references_` and `values_` arrays, or
        // -1 if it hasn't been added to them yet.
        std::ptrdiff_t arrayIndex = -1;
    };

    std::unordered_map<value_reference, exposed_variable> exposedVariables_;

    // The modifiers associated with certain variables, and a flag that
    // specifies whether they have been run on the values currently in
    // `values_`.
    std::unordered_map<value_reference, std::function<T(T, duration)>> modifiers_;
    bool hasRunModifiers_ = false;

    // The references and values of the variables that will be set next.
    std::vector<value_reference> references_;
    boost::container::vector<T> values_;

    // Filtered references and values of the values to be set next (if a filter is applied).
    std::vector<value_reference> references_filtered_;
    boost::container::vector<T> values_filtered_;
};


// Copies the contents of a contiguous-storage container or span into another.
template<typename Src, typename Tgt>
void copy_contents(Src&& src, Tgt&& tgt)
{
    assert(static_cast<std::size_t>(src.size()) <= static_cast<std::size_t>(tgt.size()));
    std::copy(src.data(), src.data() + src.size(), tgt.data());
}

template<typename T>
T get_start_value(variable_description vd)
{
    if (!vd.start) return T();
    return std::get<T>(*vd.start);
}
} // namespace


class slave_simulator::impl
{
public:
    impl(std::shared_ptr<slave> slave, std::string_view name)
        : slave_(std::move(slave))
        , name_(name)
        , modelDescription_(slave_->model_description())
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

    cosim::model_description model_description() const
    {
        return modelDescription_;
    }

    void expose_for_getting(variable_type type, value_reference ref)
    {
        switch (type) {
            case variable_type::real:
                realGetCache_.expose(ref);
                break;
            case variable_type::integer:
                integerGetCache_.expose(ref);
                break;
            case variable_type::boolean:
                booleanGetCache_.expose(ref);
                break;
            case variable_type::string:
                stringGetCache_.expose(ref);
                break;
            case variable_type::enumeration:
                COSIM_PANIC();
        }
    }

    double get_real(value_reference ref) const
    {
        return realGetCache_.get(ref);
    }

    int get_integer(value_reference ref) const
    {
        return integerGetCache_.get(ref);
    }

    bool get_boolean(value_reference ref) const
    {
        return booleanGetCache_.get(ref);
    }

    std::string_view get_string(value_reference ref) const
    {
        return stringGetCache_.get(ref);
    }

    void expose_for_setting(variable_type type, value_reference ref)
    {
        const auto vd = find_variable_description(ref, type);
        switch (type) {
            case variable_type::real:
                realSetCache_.expose(ref, get_start_value<double>(vd));
                break;
            case variable_type::integer:
                integerSetCache_.expose(ref, get_start_value<int>(vd));
                break;
            case variable_type::boolean:
                booleanSetCache_.expose(ref, get_start_value<bool>(vd));
                break;
            case variable_type::string:
                stringSetCache_.expose(ref, get_start_value<std::string>(vd));
                break;
            case variable_type::enumeration:
                COSIM_PANIC();
        }
    }

    void set_real(value_reference ref, double value)
    {
        realSetCache_.set_value(ref, value);
    }

    void set_integer(value_reference ref, int value)
    {
        integerSetCache_.set_value(ref, value);
    }

    void set_boolean(value_reference ref, bool value)
    {
        booleanSetCache_.set_value(ref, value);
    }

    void set_string(value_reference ref, std::string_view value)
    {
        stringSetCache_.set_value(ref, value);
    }

    void set_real_input_modifier(
        value_reference ref,
        std::function<double(double, duration)> modifier)
    {
        realSetCache_.set_modifier(ref, modifier);
        set_modified_reference(modifiedRealVariables_, ref, modifier ? true : false);
    }

    void set_integer_input_modifier(
        value_reference ref,
        std::function<int(int, duration)> modifier)
    {
        integerSetCache_.set_modifier(ref, modifier);
        set_modified_reference(modifiedIntegerVariables_, ref, modifier ? true : false);
    }

    void set_boolean_input_modifier(
        value_reference ref,
        std::function<bool(bool, duration)> modifier)
    {
        booleanSetCache_.set_modifier(ref, modifier);
        set_modified_reference(modifiedBooleanVariables_, ref, modifier ? true : false);
    }

    void set_string_input_modifier(
        value_reference ref,
        std::function<std::string(std::string_view, duration)> modifier)
    {
        stringSetCache_.set_modifier(ref, modifier);
        set_modified_reference(modifiedStringVariables_, ref, modifier ? true : false);
    }

    void set_real_output_modifier(
        value_reference ref,
        std::function<double(double, duration)> modifier)
    {
        realGetCache_.set_modifier(ref, modifier);
        set_modified_reference(modifiedRealVariables_, ref, modifier ? true : false);
    }

    void set_integer_output_modifier(
        value_reference ref,
        std::function<int(int, duration)> modifier)
    {
        integerGetCache_.set_modifier(ref, modifier);
        set_modified_reference(modifiedIntegerVariables_, ref, modifier ? true : false);
    }

    void set_boolean_output_modifier(
        value_reference ref,
        std::function<bool(bool, duration)> modifier)
    {
        booleanGetCache_.set_modifier(ref, modifier);
        set_modified_reference(modifiedBooleanVariables_, ref, modifier ? true : false);
    }

    void set_string_output_modifier(
        value_reference ref,
        std::function<std::string(std::string_view, duration)> modifier)
    {
        stringGetCache_.set_modifier(ref, modifier);
        set_modified_reference(modifiedStringVariables_, ref, modifier ? true : false);
    }

    std::unordered_set<value_reference>& get_modified_real_variables()
    {
        return modifiedRealVariables_;
    }

    std::unordered_set<value_reference>& get_modified_integer_variables()
    {
        return modifiedIntegerVariables_;
    }

    std::unordered_set<value_reference>& get_modified_boolean_variables()
    {
        return modifiedBooleanVariables_;
    }

    std::unordered_set<value_reference>& get_modified_string_variables()
    {
        return modifiedStringVariables_;
    }

    void setup(
        time_point startTime,
        std::optional<time_point> stopTime,
        std::optional<double> relativeTolerance)
    {
        auto deltaT = duration::zero();
        auto filter = [this](const variable_type vt) {
            return [this, vt](const value_reference vr, const cosim::scalar_value&) {
                const auto& vd = this->find_variable_description(vr, vt);
                return vd.variability != variable_variability::constant &&
                    vd.causality != variable_causality::input;
            };
        };

        const auto [realRefs, realValues] = realSetCache_.modify_and_get(deltaT, filter(variable_type::real));
        const auto [integerRefs, integerValues] = integerSetCache_.modify_and_get(deltaT, filter(variable_type::integer));
        const auto [booleanRefs, booleanValues] = booleanSetCache_.modify_and_get(deltaT, filter(variable_type::boolean));
        const auto [stringRefs, stringValues] = stringSetCache_.modify_and_get(deltaT, filter(variable_type::string));

        slave_->set_variables(
            gsl::make_span(realRefs),
            gsl::make_span(realValues),
            gsl::make_span(integerRefs),
            gsl::make_span(integerValues),
            gsl::make_span(booleanRefs),
            gsl::make_span(booleanValues),
            gsl::make_span(stringRefs),
            gsl::make_span(stringValues));

        slave_->setup(startTime, stopTime, relativeTolerance);
        get_variables(duration::zero());
    }

    void do_iteration()
    {
        set_variables(duration::zero());
        get_variables(duration::zero());
    }

    void start_simulation()
    {
        set_variables(duration::zero());
        slave_->start_simulation();
        get_variables(duration::zero());
    }

    step_result do_step(
        time_point currentT,
        duration deltaT)
    {

        set_variables(deltaT);
        const auto result = slave_->do_step(currentT, deltaT);
        get_variables(deltaT);
        return result;
    }

private:
    void set_variables(duration deltaT)
    {
        const auto [realRefs, realValues] = realSetCache_.modify_and_get(deltaT);
        const auto [integerRefs, integerValues] = integerSetCache_.modify_and_get(deltaT);
        const auto [booleanRefs, booleanValues] = booleanSetCache_.modify_and_get(deltaT);
        const auto [stringRefs, stringValues] = stringSetCache_.modify_and_get(deltaT);
        slave_->set_variables(
            gsl::make_span(realRefs),
            gsl::make_span(realValues),
            gsl::make_span(integerRefs),
            gsl::make_span(integerValues),
            gsl::make_span(booleanRefs),
            gsl::make_span(booleanValues),
            gsl::make_span(stringRefs),
            gsl::make_span(stringValues));
        realSetCache_.reset();
        integerSetCache_.reset();
        booleanSetCache_.reset();
        stringSetCache_.reset();
    }

    void get_variables(duration deltaT)
    {
        slave_->get_variables(
            &variable_values_,
            gsl::make_span(realGetCache_.references),
            gsl::make_span(integerGetCache_.references),
            gsl::make_span(booleanGetCache_.references),
            gsl::make_span(stringGetCache_.references));
        copy_contents(variable_values_.real, realGetCache_.originalValues);
        copy_contents(variable_values_.integer, integerGetCache_.originalValues);
        copy_contents(variable_values_.boolean, booleanGetCache_.originalValues);
        copy_contents(variable_values_.string, stringGetCache_.originalValues);
        realGetCache_.run_modifiers(deltaT);
        integerGetCache_.run_modifiers(deltaT);
        booleanGetCache_.run_modifiers(deltaT);
        stringGetCache_.run_modifiers(deltaT);
    }

    variable_description find_variable_description(value_reference ref, variable_type type)
    {
        auto it = std::find_if(
            modelDescription_.variables.begin(),
            modelDescription_.variables.end(),
            [type, ref](const auto& vd) { return vd.type == type && vd.reference == ref; });
        if (it == modelDescription_.variables.end()) {
            std::ostringstream oss;
            oss << "Variable with value reference " << ref
                << " and type " << type
                << " not found in model description for " << name_;
            throw std::out_of_range(oss.str());
        }
        return *it;
    }

    void set_modified_reference(std::unordered_set<value_reference>& modifiedRefs, value_reference& ref, bool modifier)
    {
        if (modifier) {
            modifiedRefs.insert(ref);
        } else {
            modifiedRefs.erase(ref);
        }
    }

private:
    std::shared_ptr<slave> slave_;
    std::string name_;
    cosim::model_description modelDescription_;

    get_variable_cache<double> realGetCache_;
    get_variable_cache<int> integerGetCache_;
    get_variable_cache<bool> booleanGetCache_;
    get_variable_cache<std::string> stringGetCache_;

    set_variable_cache<double> realSetCache_;
    set_variable_cache<int> integerSetCache_;
    set_variable_cache<bool> booleanSetCache_;
    set_variable_cache<std::string> stringSetCache_;

    std::unordered_set<value_reference> modifiedRealVariables_;
    std::unordered_set<value_reference> modifiedIntegerVariables_;
    std::unordered_set<value_reference> modifiedBooleanVariables_;
    std::unordered_set<value_reference> modifiedStringVariables_;

    cosim::slave::variable_values variable_values_;
};


slave_simulator::slave_simulator(
    std::shared_ptr<slave> slave,
    std::string_view name)
    : pimpl_(std::make_unique<impl>(std::move(slave), name))
    , state_(slave_state::created)
{
}


slave_simulator::~slave_simulator() noexcept = default;

slave_simulator::slave_simulator(slave_simulator&&) noexcept = default;

slave_simulator& slave_simulator::operator=(slave_simulator&&) noexcept = default;


std::string slave_simulator::name() const
{
    return pimpl_->name();
}


cosim::model_description slave_simulator::model_description() const
{
    COSIM_PRECONDITION(state_ != slave_state::error);
    return pimpl_->model_description();
}


void slave_simulator::expose_for_getting(variable_type type, value_reference ref)
{
    pimpl_->expose_for_getting(type, ref);
}


double slave_simulator::get_real(value_reference ref) const
{
    return pimpl_->get_real(ref);
}


int slave_simulator::get_integer(value_reference ref) const
{
    return pimpl_->get_integer(ref);
}


bool slave_simulator::get_boolean(value_reference ref) const
{
    return pimpl_->get_boolean(ref);
}


std::string_view slave_simulator::get_string(value_reference ref) const
{
    return pimpl_->get_string(ref);
}


void slave_simulator::expose_for_setting(variable_type type, value_reference ref)
{
    pimpl_->expose_for_setting(type, ref);
}


void slave_simulator::set_real(value_reference ref, double value)
{
    pimpl_->set_real(ref, value);
}


void slave_simulator::set_integer(value_reference ref, int value)
{
    pimpl_->set_integer(ref, value);
}


void slave_simulator::set_boolean(value_reference ref, bool value)
{
    pimpl_->set_boolean(ref, value);
}


void slave_simulator::set_string(value_reference ref, std::string_view value)
{
    pimpl_->set_string(ref, value);
}

void slave_simulator::set_real_input_modifier(
    value_reference ref,
    std::function<double(double, duration)> modifier)
{
    pimpl_->set_real_input_modifier(ref, modifier);
}

void slave_simulator::set_integer_input_modifier(
    value_reference ref,
    std::function<int(int, duration)> modifier)
{
    pimpl_->set_integer_input_modifier(ref, modifier);
}

void slave_simulator::set_boolean_input_modifier(
    value_reference ref,
    std::function<bool(bool, duration)> modifier)
{
    pimpl_->set_boolean_input_modifier(ref, modifier);
}

void slave_simulator::set_string_input_modifier(
    value_reference ref,
    std::function<std::string(std::string_view, duration)> modifier)
{
    pimpl_->set_string_input_modifier(ref, modifier);
}

void slave_simulator::set_real_output_modifier(
    value_reference ref,
    std::function<double(double, duration)> modifier)
{
    pimpl_->set_real_output_modifier(ref, modifier);
}

void slave_simulator::set_integer_output_modifier(
    value_reference ref,
    std::function<int(int, duration)> modifier)
{
    pimpl_->set_integer_output_modifier(ref, modifier);
}

void slave_simulator::set_boolean_output_modifier(
    value_reference ref,
    std::function<bool(bool, duration)> modifier)
{
    pimpl_->set_boolean_output_modifier(ref, modifier);
}

void slave_simulator::set_string_output_modifier(
    value_reference ref,
    std::function<std::string(std::string_view, duration)> modifier)
{
    pimpl_->set_string_output_modifier(ref, modifier);
}

std::unordered_set<value_reference>& slave_simulator::get_modified_real_variables() const
{
    return pimpl_->get_modified_real_variables();
}

std::unordered_set<value_reference>& slave_simulator::get_modified_integer_variables() const
{
    return pimpl_->get_modified_integer_variables();
}

std::unordered_set<value_reference>& slave_simulator::get_modified_boolean_variables() const
{
    return pimpl_->get_modified_boolean_variables();
}

std::unordered_set<value_reference>& slave_simulator::get_modified_string_variables() const
{
    return pimpl_->get_modified_string_variables();
}

void slave_simulator::setup(
    time_point startTime,
    std::optional<time_point> stopTime,
    std::optional<double> relativeTolerance)
{
    COSIM_PRECONDITION(state_ == slave_state::created);
    state_guard guard(state_, slave_state::initialisation);
    return pimpl_->setup(startTime, stopTime, relativeTolerance);
}

void slave_simulator::do_iteration()
{
    return pimpl_->do_iteration();
}

void slave_simulator::start_simulation()
{
    COSIM_PRECONDITION(state_ == slave_state::initialisation);
    state_guard guard(state_, slave_state::simulation);
    return pimpl_->start_simulation();
}

step_result slave_simulator::do_step(
    time_point currentT,
    duration deltaT)
{
    COSIM_PRECONDITION(state_ == slave_state::simulation);
    state_guard guard(state_);
    return pimpl_->do_step(currentT, deltaT);
}

} // namespace cosim
