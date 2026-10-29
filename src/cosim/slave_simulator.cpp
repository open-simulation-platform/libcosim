/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/slave_simulator.hpp"

#include "cosim/error.hpp"
#include "cosim/exception.hpp"
#include "cosim/utility/enumeration.hpp"

#include <charconv>
#include <cosim/utility/utility.hpp>

#include <algorithm>
#include <cassert>
#include <set>
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

    bool has_modifier(value_reference r) const
    {
        const auto it = indexMapping.find(r);
        return it != indexMapping.end() && static_cast<bool>(modifiers[it->second]);
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

    serialization::node export_state() const
    {
        assert(references.size() == originalValues.size());
        assert(std::all_of(modifiers.begin(), modifiers.end(), [](auto m) { return !m; }));
        serialization::node exportedState;
        for (std::size_t i = 0; i < references.size(); ++i) {
            exportedState.put(
                std::to_string(references[i]),
                originalValues[i]);
        }
        return exportedState;
    }

    void import_state(const serialization::node& exportedState)
    {
        std::vector<value_reference> importedReferences;
        boost::container::vector<T> importedValues;
        std::unordered_map<value_reference, std::size_t> newMapping;
        for (const auto& [key, child] : exportedState) {
            const value_reference ref = std::stoul(key);
            const auto val = child.template get_value<T>();
            importedReferences.push_back(ref);
            importedValues.push_back(val);
            newMapping[ref] = importedReferences.size() - 1;
        }
        references.swap(importedReferences);
        originalValues.swap(importedValues);
        modifiedValues = originalValues; // copy
        modifiers.assign(modifiedValues.size(), nullptr);
        indexMapping.swap(newMapping);
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
        if (!make_cache_slot(r, it->second)) {
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
        make_cache_slot(r, it->second);
        if (m) {
            modifiers_[r] = m;
        } else {
            modifiers_.erase(r);
        }
    }

    bool has_modifier(value_reference r) const
    {
        return modifiers_.count(r) != 0;
    }

    std::pair<gsl::span<value_reference>, gsl::span<const T>> modify_and_get(
        duration deltaT,
        std::optional<std::function<bool(const value_reference, const T&)>> filter = std::nullopt)
    {
        if (!hasRunModifiers_) {
            for (const auto& entry : modifiers_) {
                const auto ref = entry.first;
                auto& exposedVariable = exposedVariables_.at(ref);
                make_cache_slot(ref, exposedVariable);
                values_[exposedVariable.arrayIndex] =
                    entry.second(values_[exposedVariable.arrayIndex], deltaT);
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

    serialization::node export_state() const
    {
        assert(modifiers_.empty());
        serialization::node exportedState;
        for (const auto& [ref, var] : exposedVariables_) {
            exportedState.put(std::to_string(ref), var.lastValue);
        }
        return exportedState;
    }

    void import_state(const serialization::node& exportedState)
    {
        assert(modifiers_.empty());
        std::unordered_map<value_reference, exposed_variable> variables;
        for (const auto& [key, child] : exportedState) {
            variables.emplace(
                std::stoul(key),
                exposed_variable{child.template get_value<T>()});
        }
        exposedVariables_.swap(variables);
        hasRunModifiers_ = false;
        references_.clear();
        values_.clear();
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

    // If the given `exposed_variable` does not yet have slots in the
    // `references_` and `values_` arrays, this function creates them and
    // returns `true`.  Otherwise, it returns `false` to signify that no new
    // slot needed to be created.
    bool make_cache_slot(value_reference ref, exposed_variable& var)
    {
        if (var.arrayIndex < 0) {
            var.arrayIndex = references_.size();
            assert(references_.size() == values_.size());
            references_.emplace_back(ref);
            values_.emplace_back(var.lastValue);
            return true;
        } else {
            return false;
        }
    }

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

template<typename Values, typename Scalar>
Scalar apply_scalar_modifier(
    const manipulable::variable_modifier& modifier,
    Scalar original,
    duration deltaT)
{
    auto modified = modifier(variable_value(Values{{original}}), deltaT);
    if (modified.type() != Values::type || !modified.is_scalar()) {
        throw error(make_error_code(errc::simulation_error),
            "A whole-variable modifier changed the value type or shape");
    }
    return std::get<Values>(modified.values()).values[0];
}

variable_value apply_enumeration_modifier(
    const manipulable::variable_modifier& modifier,
    enumeration_value original,
    duration deltaT)
{
    auto modified = modifier(variable_value(enumeration_values{{original}}), deltaT);
    if (const auto normalized =
            normalize_enumeration_value(variable_type::enumeration, modified)) {
        modified = *normalized;
    }
    if (modified.type() != variable_type::enumeration || !modified.is_scalar()) {
        throw error(make_error_code(errc::simulation_error),
            "A whole-variable modifier changed the value type or shape");
    }
    return modified;
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
                state_.realGetCache.expose(ref);
                return;
            case variable_type::integer:
                state_.integerGetCache.expose(ref);
                return;
            case variable_type::boolean:
                state_.booleanGetCache.expose(ref);
                return;
            case variable_type::string:
                state_.stringGetCache.expose(ref);
                return;
            case variable_type::enumeration:
                if (!find_variable_description(ref, type).dimensions.empty()) {
                    throw error(make_error_code(errc::unsupported_feature),
                        "Array Enumeration access is not supported");
                }
                state_.enumerationGetCache.try_emplace(ref, std::nullopt);
                return;
            case variable_type::float32:
            case variable_type::int8:
            case variable_type::uint8:
            case variable_type::int16:
            case variable_type::uint16:
            case variable_type::uint32:
            case variable_type::int64:
            case variable_type::uint64:
            case variable_type::binary:
            case variable_type::clock:
                COSIM_PANIC();
        }
        COSIM_PANIC();
    }

    double get_real(value_reference ref) const
    {
        return state_.realGetCache.get(ref);
    }

    int get_integer(value_reference ref) const
    {
        return state_.integerGetCache.get(ref);
    }

    bool get_boolean(value_reference ref) const
    {
        return state_.booleanGetCache.get(ref);
    }

    std::string_view get_string(value_reference ref) const
    {
        return state_.stringGetCache.get(ref);
    }

    variable_value get_enumeration_value(value_reference ref) const
    {
        const auto& value = state_.enumerationGetCache.at(ref);
        if (!value) {
            throw std::logic_error("Exposed Enumeration variable has not been retrieved yet");
        }
        return variable_value(enumeration_values{{*value}});
    }

    void expose_for_setting(variable_type type, value_reference ref)
    {
        const auto vd = find_variable_description(ref, type);
        switch (type) {
            case variable_type::real:
                state_.realSetCache.expose(ref, get_start_value<double>(vd));
                return;
            case variable_type::integer:
                state_.integerSetCache.expose(ref, get_start_value<int>(vd));
                return;
            case variable_type::boolean:
                state_.booleanSetCache.expose(ref, get_start_value<bool>(vd));
                return;
            case variable_type::string:
                state_.stringSetCache.expose(ref, get_start_value<std::string>(vd));
                return;
            case variable_type::enumeration:
                if (!vd.dimensions.empty()) {
                    throw error(make_error_code(errc::unsupported_feature),
                        "Array Enumeration access is not supported");
                }
                {
                    std::optional<enumeration_value> start;
                    if (vd.start) start = std::get<enumeration_value>(*vd.start);
                    state_.enumerationSetCache.try_emplace(
                        ref, enumeration_set_entry{start, false});
                }
                return;
            case variable_type::float32:
            case variable_type::int8:
            case variable_type::uint8:
            case variable_type::int16:
            case variable_type::uint16:
            case variable_type::uint32:
            case variable_type::int64:
            case variable_type::uint64:
            case variable_type::binary:
            case variable_type::clock:
                COSIM_PANIC();
        }
        COSIM_PANIC();
    }

    void set_real(value_reference ref, double value)
    {
        state_.realSetCache.set_value(ref, value);
    }

    void set_integer(value_reference ref, int value)
    {
        state_.integerSetCache.set_value(ref, value);
    }

    void set_boolean(value_reference ref, bool value)
    {
        state_.booleanSetCache.set_value(ref, value);
    }

    void set_string(value_reference ref, std::string_view value)
    {
        state_.stringSetCache.set_value(ref, value);
    }

    void set_enumeration_value(value_reference ref, const variable_value& value)
    {
        if (!value.is_scalar()) {
            throw std::invalid_argument("Enumeration assignment must be scalar");
        }
        enumeration_value normalized;
        if (value.type() == variable_type::enumeration) {
            normalized = std::get<enumeration_values>(value.values()).values[0];
        } else if (value.type() == variable_type::integer) {
            normalized.value = std::get<integer_values>(value.values()).values[0];
        } else {
            throw std::invalid_argument("Enumeration assignment type mismatch");
        }
        auto& entry = state_.enumerationSetCache.at(ref);
        entry.value = normalized;
        entry.pending = true;
    }

    void set_variable_input_modifier(
        variable_type type, value_reference ref, manipulable::variable_modifier modifier)
    {
        if (type == variable_type::enumeration) {
            auto& entry = state_.enumerationSetCache.at(ref);
            if (modifier) {
                enumerationInputModifiers_[ref] = std::move(modifier);
            } else {
                enumerationInputModifiers_.erase(ref);
                entry.pending = entry.value.has_value();
            }
            return;
        }
        switch (type) {
            case variable_type::real:
                set_real_input_modifier(ref, modifier ? [modifier](double v, duration dt) {
                    return apply_scalar_modifier<real_values>(modifier, v, dt);
                } : std::function<double(double, duration)>{});
                return;
            case variable_type::integer:
                set_integer_input_modifier(ref, modifier ? [modifier](int v, duration dt) {
                    return apply_scalar_modifier<integer_values>(modifier, v, dt);
                } : std::function<int(int, duration)>{});
                return;
            case variable_type::boolean:
                set_boolean_input_modifier(ref, modifier ? [modifier](bool v, duration dt) {
                    return apply_scalar_modifier<boolean_values>(modifier, v, dt);
                } : std::function<bool(bool, duration)>{});
                return;
            case variable_type::string:
                set_string_input_modifier(ref, modifier ? [modifier](std::string_view v, duration dt) {
                    return apply_scalar_modifier<string_values>(modifier, std::string(v), dt);
                } : std::function<std::string(std::string_view, duration)>{});
                return;
            case variable_type::enumeration:
            case variable_type::float32:
            case variable_type::int8:
            case variable_type::uint8:
            case variable_type::int16:
            case variable_type::uint16:
            case variable_type::uint32:
            case variable_type::int64:
            case variable_type::uint64:
            case variable_type::binary:
            case variable_type::clock:
                break;
        }
        throw error(make_error_code(errc::unsupported_feature),
            "Whole-value input modifier type is not supported");
    }

    void set_variable_output_modifier(
        variable_type type, value_reference ref, manipulable::variable_modifier modifier)
    {
        if (type == variable_type::enumeration) {
            (void)state_.enumerationGetCache.at(ref);
            if (modifier) {
                enumerationOutputModifiers_[ref] = std::move(modifier);
            } else {
                enumerationOutputModifiers_.erase(ref);
            }
            return;
        }
        switch (type) {
            case variable_type::real:
                set_real_output_modifier(ref, modifier ? [modifier](double v, duration dt) {
                    return apply_scalar_modifier<real_values>(modifier, v, dt);
                } : std::function<double(double, duration)>{});
                return;
            case variable_type::integer:
                set_integer_output_modifier(ref, modifier ? [modifier](int v, duration dt) {
                    return apply_scalar_modifier<integer_values>(modifier, v, dt);
                } : std::function<int(int, duration)>{});
                return;
            case variable_type::boolean:
                set_boolean_output_modifier(ref, modifier ? [modifier](bool v, duration dt) {
                    return apply_scalar_modifier<boolean_values>(modifier, v, dt);
                } : std::function<bool(bool, duration)>{});
                return;
            case variable_type::string:
                set_string_output_modifier(ref, modifier ? [modifier](std::string_view v, duration dt) {
                    return apply_scalar_modifier<string_values>(modifier, std::string(v), dt);
                } : std::function<std::string(std::string_view, duration)>{});
                return;
            case variable_type::enumeration:
            case variable_type::float32:
            case variable_type::int8:
            case variable_type::uint8:
            case variable_type::int16:
            case variable_type::uint16:
            case variable_type::uint32:
            case variable_type::int64:
            case variable_type::uint64:
            case variable_type::binary:
            case variable_type::clock:
                break;
        }
        throw error(make_error_code(errc::unsupported_feature),
            "Whole-value output modifier type is not supported");
    }

    void set_real_input_modifier(
        value_reference ref,
        std::function<double(double, duration)> modifier)
    {
        state_.realSetCache.set_modifier(ref, modifier);
        set_modified_reference(modifiedRealVariables_, ref, state_.realGetCache, state_.realSetCache);
    }

    void set_integer_input_modifier(
        value_reference ref,
        std::function<int(int, duration)> modifier)
    {
        state_.integerSetCache.set_modifier(ref, modifier);
        set_modified_reference(modifiedIntegerVariables_, ref, state_.integerGetCache, state_.integerSetCache);
    }

    void set_boolean_input_modifier(
        value_reference ref,
        std::function<bool(bool, duration)> modifier)
    {
        state_.booleanSetCache.set_modifier(ref, modifier);
        set_modified_reference(modifiedBooleanVariables_, ref, state_.booleanGetCache, state_.booleanSetCache);
    }

    void set_string_input_modifier(
        value_reference ref,
        std::function<std::string(std::string_view, duration)> modifier)
    {
        state_.stringSetCache.set_modifier(ref, modifier);
        set_modified_reference(modifiedStringVariables_, ref, state_.stringGetCache, state_.stringSetCache);
    }

    void set_real_output_modifier(
        value_reference ref,
        std::function<double(double, duration)> modifier)
    {
        state_.realGetCache.set_modifier(ref, modifier);
        set_modified_reference(modifiedRealVariables_, ref, state_.realGetCache, state_.realSetCache);
    }

    void set_integer_output_modifier(
        value_reference ref,
        std::function<int(int, duration)> modifier)
    {
        state_.integerGetCache.set_modifier(ref, modifier);
        set_modified_reference(modifiedIntegerVariables_, ref, state_.integerGetCache, state_.integerSetCache);
    }

    void set_boolean_output_modifier(
        value_reference ref,
        std::function<bool(bool, duration)> modifier)
    {
        state_.booleanGetCache.set_modifier(ref, modifier);
        set_modified_reference(modifiedBooleanVariables_, ref, state_.booleanGetCache, state_.booleanSetCache);
    }

    void set_string_output_modifier(
        value_reference ref,
        std::function<std::string(std::string_view, duration)> modifier)
    {
        state_.stringGetCache.set_modifier(ref, modifier);
        set_modified_reference(modifiedStringVariables_, ref, state_.stringGetCache, state_.stringSetCache);
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

    std::vector<typed_value_reference> get_modified_variables() const
    {
        std::set<std::pair<variable_type, value_reference>> modified;
        const auto add = [&modified](variable_type type, const auto& references) {
            for (const auto reference : references) modified.emplace(type, reference);
        };
        add(variable_type::real, modifiedRealVariables_);
        add(variable_type::integer, modifiedIntegerVariables_);
        add(variable_type::boolean, modifiedBooleanVariables_);
        add(variable_type::string, modifiedStringVariables_);
        for (const auto& entry : enumerationInputModifiers_) {
            modified.emplace(variable_type::enumeration, entry.first);
        }
        for (const auto& entry : enumerationOutputModifiers_) {
            modified.emplace(variable_type::enumeration, entry.first);
        }
        std::vector<typed_value_reference> result;
        result.reserve(modified.size());
        for (const auto& entry : modified) result.push_back({entry.first, entry.second});
        return result;
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

        const auto [realRefs, realValues] = state_.realSetCache.modify_and_get(deltaT, filter(variable_type::real));
        const auto [integerRefs, integerValues] = state_.integerSetCache.modify_and_get(deltaT, filter(variable_type::integer));
        const auto [booleanRefs, booleanValues] = state_.booleanSetCache.modify_and_get(deltaT, filter(variable_type::boolean));
        const auto [stringRefs, stringValues] = state_.stringSetCache.modify_and_get(deltaT, filter(variable_type::string));

        slave_->set_variables(
            gsl::make_span(realRefs),
            gsl::make_span(realValues),
            gsl::make_span(integerRefs),
            gsl::make_span(integerValues),
            gsl::make_span(booleanRefs),
            gsl::make_span(booleanValues),
            gsl::make_span(stringRefs),
            gsl::make_span(stringValues));

        set_enumerations(true, deltaT);
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

    simulator::state_index save_state()
    {
        check_state_saving_allowed();
        const auto stateIndex = slave_->save_state();
        savedStates_.emplace(stateIndex, state_);
        return stateIndex;
    }

    void save_state(simulator::state_index stateIndex)
    {
        check_state_saving_allowed();
        slave_->save_state(stateIndex);
        savedStates_.at(stateIndex) = state_;
    }

    void restore_state(simulator::state_index stateIndex)
    {
        check_state_saving_allowed();
        slave_->restore_state(stateIndex);
        state_ = savedStates_.at(stateIndex);
    }

    void release_state(simulator::state_index stateIndex)
    {
        slave_->release_state(stateIndex);
        savedStates_.erase(stateIndex);
    }

    // IMPORTANT:
    // Since the cosim::slave_simulator class may evolve over time, and the
    // state that needs to be serialized may change, we need some versioning.
    // Increment this number whenever the "exported state" changes form, and
    // always consider whether backwards compatibility measures are warranted.
    static constexpr std::int32_t export_scheme_version = 0;

    serialization::node export_state(state_index stateIndex) const
    {
        serialization::node exportedState;
        exportedState.put("scheme_version", export_scheme_version);
        exportedState.put_child("state", slave_->export_state(stateIndex));
        const auto& savedState = savedStates_.at(stateIndex);
        exportedState.put_child("real_get_cache", savedState.realGetCache.export_state());
        exportedState.put_child("integer_get_cache", savedState.integerGetCache.export_state());
        exportedState.put_child("boolean_get_cache", savedState.booleanGetCache.export_state());
        exportedState.put_child("string_get_cache", savedState.stringGetCache.export_state());
        exportedState.put_child("real_set_cache", savedState.realSetCache.export_state());
        exportedState.put_child("integer_set_cache", savedState.integerSetCache.export_state());
        exportedState.put_child("boolean_set_cache", savedState.booleanSetCache.export_state());
        exportedState.put_child("string_set_cache", savedState.stringSetCache.export_state());
        serialization::node enumerationGet;
        for (const auto& [reference, value] : savedState.enumerationGetCache) {
            serialization::node entry;
            entry.put("has_value", value.has_value());
            if (value) entry.put("value", value->value);
            enumerationGet.put_child(std::to_string(reference), entry);
        }
        exportedState.put_child("enumeration_get_cache", enumerationGet);
        serialization::node enumerationSet;
        for (const auto& [reference, cached] : savedState.enumerationSetCache) {
            serialization::node entry;
            entry.put("has_value", cached.value.has_value());
            entry.put("pending", cached.pending);
            if (cached.value) entry.put("value", cached.value->value);
            enumerationSet.put_child(std::to_string(reference), entry);
        }
        exportedState.put_child("enumeration_set_cache", enumerationSet);
        return exportedState;
    }

    state_index import_state(const serialization::node& exportedState)
    {
        try {
            if (exportedState.get<std::int32_t>("scheme_version")
                    != export_scheme_version) {
                throw error(
                    make_error_code(errc::bad_file),
                    "The serialized state of subsimulator '" + name_
                        + "' uses an incompatible scheme");
            }
            state savedState;
            savedState.realGetCache.import_state(exportedState.get_child("real_get_cache"));
            savedState.integerGetCache.import_state(exportedState.get_child("integer_get_cache"));
            savedState.booleanGetCache.import_state(exportedState.get_child("boolean_get_cache"));
            savedState.stringGetCache.import_state(exportedState.get_child("string_get_cache"));
            savedState.realSetCache.import_state(exportedState.get_child("real_set_cache"));
            savedState.integerSetCache.import_state(exportedState.get_child("integer_set_cache"));
            savedState.booleanSetCache.import_state(exportedState.get_child("boolean_set_cache"));
            savedState.stringSetCache.import_state(exportedState.get_child("string_set_cache"));
            const auto parseReference = [](const std::string& key) {
                value_reference reference{};
                const auto result =
                    std::from_chars(key.data(), key.data() + key.size(), reference);
                if (result.ec != std::errc{} || result.ptr != key.data() + key.size()) {
                    throw error(make_error_code(errc::bad_file),
                        "Invalid Enumeration cache reference");
                }
                return reference;
            };
            const auto importEnumerations = [&parseReference](const serialization::node& node, auto& cache) {
                for (const auto& [key, entry] : node) {
                    const auto reference = parseReference(key);
                    std::optional<enumeration_value> value;
                    if (entry.template get<bool>("has_value")) {
                        value = enumeration_value{
                            entry.template get<std::int64_t>("value")};
                    }
                    if (!cache.emplace(reference, value).second) {
                        throw error(make_error_code(errc::bad_file),
                            "Duplicate Enumeration cache reference");
                    }
                }
            };
            const auto getNode = exportedState.get_child_optional("enumeration_get_cache");
            const auto setNode = exportedState.get_child_optional("enumeration_set_cache");
            if (static_cast<bool>(getNode) != static_cast<bool>(setNode)) {
                throw error(make_error_code(errc::bad_file),
                    "Incomplete Enumeration caches in serialized state");
            }
            if (getNode) {
                importEnumerations(*getNode, savedState.enumerationGetCache);
            } else {
                for (const auto& [reference, value] : state_.enumerationGetCache) {
                    savedState.enumerationGetCache.emplace(reference, std::nullopt);
                }
            }
            if (setNode) {
                for (const auto& [key, entry] : *setNode) {
                    const auto reference = parseReference(key);
                    std::optional<enumeration_value> value;
                    if (entry.get<bool>("has_value")) {
                        value = enumeration_value{entry.get<std::int64_t>("value")};
                    }
                    const auto pending = entry.get<bool>("pending");
                    if (pending && !value) {
                        throw error(make_error_code(errc::bad_file),
                            "Pending Enumeration cache entry has no value");
                    }
                    if (!savedState.enumerationSetCache.emplace(
                            reference, enumeration_set_entry{value, pending}).second) {
                        throw error(make_error_code(errc::bad_file),
                            "Duplicate Enumeration cache reference");
                    }
                }
            } else {
                for (const auto& [reference, cached] : state_.enumerationSetCache) {
                    savedState.enumerationSetCache.emplace(
                        reference, enumeration_set_entry{std::nullopt, false});
                }
            }
            const auto stateIndex =
                slave_->import_state(exportedState.get_child("state"));
            assert(savedStates_.count(stateIndex) == 0);
            try {
                savedStates_.emplace(stateIndex, std::move(savedState));
            } catch (...) {
                slave_->release_state(stateIndex);
                throw;
            }
            return stateIndex;
        } catch (const boost::property_tree::ptree_bad_path&) {
            throw error(
                make_error_code(errc::bad_file),
                "The serialized state of subsimulator '" + name_
                    + "' is invalid or corrupt");
        } catch (const std::bad_variant_access&) {
            throw error(
                make_error_code(errc::bad_file),
                "The serialized state of subsimulator '" + name_
                    + "' is invalid or corrupt");
        } catch (const boost::property_tree::ptree_bad_data&) {
            throw error(
                make_error_code(errc::bad_file),
                "The serialized state of subsimulator '" + name_
                    + "' is invalid or corrupt");
        } catch (const std::invalid_argument&) {
            throw error(
                make_error_code(errc::bad_file),
                "The serialized state of subsimulator '" + name_
                    + "' is invalid or corrupt");
        } catch (const std::out_of_range&) {
            throw error(
                make_error_code(errc::bad_file),
                "The serialized state of subsimulator '" + name_
                    + "' is invalid or corrupt");
        }
    }

private:
    void set_enumerations(bool beforeSetup, duration deltaT)
    {
        std::vector<typed_value_reference> references;
        std::vector<variable_value> values;
        for (const auto& [reference, cached] : state_.enumerationSetCache) {
            const auto modifier = enumerationInputModifiers_.find(reference);
            if (!cached.pending && modifier == enumerationInputModifiers_.end()) continue;
            if (!cached.value) {
                throw error(make_error_code(errc::simulation_error),
                    "Cannot modify an Enumeration variable without an initial value");
            }
            if (beforeSetup) {
                const auto description =
                    find_variable_description(reference, variable_type::enumeration);
                if (description.variability == variable_variability::constant ||
                    description.causality == variable_causality::input) {
                    continue;
                }
            }
            references.push_back({variable_type::enumeration, reference});
            values.push_back(modifier == enumerationInputModifiers_.end()
                ? variable_value(enumeration_values{{*cached.value}})
                : apply_enumeration_modifier(modifier->second, *cached.value, deltaT));
        }
        if (references.empty()) return;
        slave_->set_variable_values(references, values);
        for (const auto variable : references) {
            state_.enumerationSetCache.at(variable.reference).pending = false;
        }
    }

    void set_variables(duration deltaT)
    {
        const auto [realRefs, realValues] = state_.realSetCache.modify_and_get(deltaT);
        const auto [integerRefs, integerValues] = state_.integerSetCache.modify_and_get(deltaT);
        const auto [booleanRefs, booleanValues] = state_.booleanSetCache.modify_and_get(deltaT);
        const auto [stringRefs, stringValues] = state_.stringSetCache.modify_and_get(deltaT);
        slave_->set_variables(
            gsl::make_span(realRefs),
            gsl::make_span(realValues),
            gsl::make_span(integerRefs),
            gsl::make_span(integerValues),
            gsl::make_span(booleanRefs),
            gsl::make_span(booleanValues),
            gsl::make_span(stringRefs),
            gsl::make_span(stringValues));
        set_enumerations(false, deltaT);
        state_.realSetCache.reset();
        state_.integerSetCache.reset();
        state_.booleanSetCache.reset();
        state_.stringSetCache.reset();
    }

    void get_variables(duration deltaT)
    {
        slave_->get_variables(
            &variableValues_,
            gsl::make_span(state_.realGetCache.references),
            gsl::make_span(state_.integerGetCache.references),
            gsl::make_span(state_.booleanGetCache.references),
            gsl::make_span(state_.stringGetCache.references));
        copy_contents(variableValues_.real, state_.realGetCache.originalValues);
        copy_contents(variableValues_.integer, state_.integerGetCache.originalValues);
        copy_contents(variableValues_.boolean, state_.booleanGetCache.originalValues);
        copy_contents(variableValues_.string, state_.stringGetCache.originalValues);
        state_.realGetCache.run_modifiers(deltaT);
        state_.integerGetCache.run_modifiers(deltaT);
        state_.booleanGetCache.run_modifiers(deltaT);
        state_.stringGetCache.run_modifiers(deltaT);
        std::vector<typed_value_reference> references;
        for (const auto& [reference, value] : state_.enumerationGetCache) {
            references.push_back({variable_type::enumeration, reference});
        }
        if (!references.empty()) {
            const auto values = slave_->get_variable_values(references);
            if (values.size() != references.size()) {
                throw error(make_error_code(errc::model_error),
                    "Slave returned an incorrect number of Enumeration values");
            }
            for (std::size_t i = 0; i < values.size(); ++i) {
                if (values[i].type() != variable_type::enumeration ||
                    !values[i].is_scalar()) {
                    throw error(make_error_code(errc::model_error),
                        "Slave returned an invalid Enumeration value");
                }
            }
            for (std::size_t i = 0; i < values.size(); ++i) {
                auto value = std::get<enumeration_values>(values[i].values()).values[0];
                const auto modifier = enumerationOutputModifiers_.find(references[i].reference);
                if (modifier != enumerationOutputModifiers_.end()) {
                    const auto modified = apply_enumeration_modifier(modifier->second, value, deltaT);
                    value = std::get<enumeration_values>(modified.values()).values[0];
                }
                state_.enumerationGetCache.at(references[i].reference) = value;
            }
        }
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

    template<typename T>
    void set_modified_reference(
        std::unordered_set<value_reference>& modifiedRefs,
        value_reference ref,
        const get_variable_cache<T>& getCache,
        const set_variable_cache<T>& setCache)
    {
        if (getCache.has_modifier(ref) || setCache.has_modifier(ref)) {
            modifiedRefs.insert(ref);
        } else {
            modifiedRefs.erase(ref);
        }
    }

    void check_state_saving_allowed() const
    {
        if (modifiedRealVariables_.empty() && modifiedIntegerVariables_.empty() &&
            modifiedBooleanVariables_.empty() && modifiedStringVariables_.empty() &&
            enumerationInputModifiers_.empty() && enumerationOutputModifiers_.empty()) {
            return;
        }
        throw error(
            make_error_code(errc::unsupported_feature),
            "Cannot save or restore subsimulator state when variable modifiers are active");
    }

private:
    struct enumeration_set_entry
    {
        std::optional<enumeration_value> value;
        bool pending = false;
    };

    std::shared_ptr<slave> slave_;
    std::string name_;
    cosim::model_description modelDescription_;

    struct state
    {
        get_variable_cache<double> realGetCache;
        get_variable_cache<int> integerGetCache;
        get_variable_cache<bool> booleanGetCache;
        get_variable_cache<std::string> stringGetCache;

        set_variable_cache<double> realSetCache;
        set_variable_cache<int> integerSetCache;
        set_variable_cache<bool> booleanSetCache;
        set_variable_cache<std::string> stringSetCache;
        std::unordered_map<value_reference, std::optional<enumeration_value>>
            enumerationGetCache;
        std::unordered_map<value_reference, enumeration_set_entry>
            enumerationSetCache;
    };
    state state_;
    std::unordered_map<state_index, state> savedStates_;

    std::unordered_set<value_reference> modifiedRealVariables_;
    std::unordered_set<value_reference> modifiedIntegerVariables_;
    std::unordered_set<value_reference> modifiedBooleanVariables_;
    std::unordered_set<value_reference> modifiedStringVariables_;
    std::unordered_map<value_reference, manipulable::variable_modifier> enumerationInputModifiers_;
    std::unordered_map<value_reference, manipulable::variable_modifier> enumerationOutputModifiers_;

    // This is a temporary storage which gets reused within certain functions
    // to avoid frequent reallocations.
    cosim::slave::variable_values variableValues_;
};


slave_simulator::slave_simulator(
    std::shared_ptr<slave> slave,
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


cosim::model_description slave_simulator::model_description() const
{
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

variable_value slave_simulator::get_variable_value(
    variable_type type, value_reference ref) const
{
    if (type == variable_type::enumeration) {
        return pimpl_->get_enumeration_value(ref);
    }
    return observable::get_variable_value(type, ref);
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

void slave_simulator::set_variable_value(
    variable_type type, value_reference ref, const variable_value& value)
{
    if (type == variable_type::enumeration) {
        pimpl_->set_enumeration_value(ref, value);
    } else {
        simulator::set_variable_value(type, ref, value);
    }
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

void slave_simulator::set_variable_input_modifier(
    variable_type type, value_reference ref, variable_modifier modifier)
{
    pimpl_->set_variable_input_modifier(type, ref, std::move(modifier));
}

void slave_simulator::set_variable_output_modifier(
    variable_type type, value_reference ref, variable_modifier modifier)
{
    pimpl_->set_variable_output_modifier(type, ref, std::move(modifier));
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

std::vector<typed_value_reference> slave_simulator::get_modified_variables() const
{
    return pimpl_->get_modified_variables();
}

void slave_simulator::setup(
    time_point startTime,
    std::optional<time_point> stopTime,
    std::optional<double> relativeTolerance)
{
    return pimpl_->setup(startTime, stopTime, relativeTolerance);
}

void slave_simulator::do_iteration()
{
    return pimpl_->do_iteration();
}

void slave_simulator::start_simulation()
{
    return pimpl_->start_simulation();
}

step_result slave_simulator::do_step(
    time_point currentT,
    duration deltaT)
{
    return pimpl_->do_step(currentT, deltaT);
}

simulator::state_index slave_simulator::save_state()
{
    return pimpl_->save_state();
}

void slave_simulator::save_state(state_index stateIndex)
{
    pimpl_->save_state(stateIndex);
}

void slave_simulator::restore_state(state_index stateIndex)
{
    pimpl_->restore_state(stateIndex);
}

void slave_simulator::release_state(state_index stateIndex)
{
    pimpl_->release_state(stateIndex);
}

serialization::node slave_simulator::export_state(state_index stateIndex) const
{
    return pimpl_->export_state(stateIndex);
}

simulator::state_index slave_simulator::import_state(
    const serialization::node& exportedState)
{
    return pimpl_->import_state(exportedState);
}


} // namespace cosim
