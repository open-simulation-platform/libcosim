/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/slave_simulator.hpp"

#include "event_value.hpp"

#include "cosim/algorithm/simulator.hpp"
#include "cosim/error.hpp"
#include "cosim/exception.hpp"
#include "cosim/manipulator/manipulator.hpp"
#include "cosim/model_description.hpp"
#include "cosim/serialization.hpp"
#include "cosim/slave.hpp"
#include "cosim/time.hpp"
#include "cosim/utility/enumeration.hpp"

#include <boost/algorithm/cxx11/any_of.hpp>
#include <boost/container/vector.hpp>
#include <boost/property_tree/exceptions.hpp>
#include <boost/range/algorithm/find.hpp>
#include <boost/range/algorithm/find_if.hpp>
#include <boost/range/algorithm/remove_if.hpp>
#include <gsl/span>
#include <gsl/span_ext>

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

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
struct get_variable_cache // NOLINT(bugprone-exception-escape): MSVC unordered-container moves may allocate.
{
    std::vector<value_reference> references;
    boost::container::vector<T> originalValues;
    boost::container::vector<T> modifiedValues;
    boost::container::vector<T> lastModifierValues;
    std::vector<std::function<T(T, duration)>> modifiers;
    std::vector<bool> modifierDirty;
    std::unordered_map<value_reference, std::size_t> indexMapping;

    void expose(value_reference reference)
    {
        if (indexMapping.count(reference) != 0U) {
            return;
        }
        references.push_back(reference);
        originalValues.push_back(
            T()); // TODO: Use start value from model description
        modifiedValues.push_back(T());
        lastModifierValues.push_back(T());
        modifiers.emplace_back();
        modifierDirty.push_back(true);
        indexMapping.emplace(reference, references.size() - 1);
    }

    [[nodiscard]] typename var_view_type<T>::type
    get(value_reference reference) const
    {
        const auto found = indexMapping.find(reference);
        if (found != indexMapping.end()) {
            return modifiedValues.at(found->second);
        } else {
            std::ostringstream oss;
            oss << "Variable with reference " << reference
                << " not found in exposed variables. Variables must be exposed "
                   "before calling get()";
            throw std::out_of_range(oss.str());
        }
    }

    [[nodiscard]] typename var_view_type<T>::type
    get_original(value_reference reference) const
    {
        return originalValues.at(indexMapping.at(reference));
    }

    void set_modifier(value_reference reference,
        std::function<T(T, duration)> modifier)
    {
        const auto found = indexMapping.find(reference);
        if (found == indexMapping.end()) {
            std::ostringstream oss;
            oss << "Variable with value reference " << reference
                << " not found in exposed variables. Variables must be exposed "
                   "before calling set_modifier()";
            throw std::out_of_range(oss.str());
        }
        modifiers.at(found->second) = std::move(modifier);
        modifierDirty.at(found->second) = true;
    }

    [[nodiscard]] bool has_modifier(value_reference reference) const
    {
        const auto found = indexMapping.find(reference);
        return found != indexMapping.end() &&
            static_cast<bool>(modifiers.at(found->second));
    }

    void run_modifiers(duration deltaT, bool reuseUnchanged = false)
    {
        for (std::size_t i = 0; i < originalValues.size(); ++i) {
            if (modifiers.at(i)) {
                if (!reuseUnchanged || modifierDirty.at(i) ||
                    !detail::event_values_equal(originalValues.at(i),
                        lastModifierValues.at(i))) {
                    modifiedValues.at(i) = modifiers.at(i)(originalValues.at(i), deltaT);
                }
            } else {
                modifiedValues.at(i) = originalValues.at(i);
            }
            lastModifierValues.at(i) = originalValues.at(i);
            modifierDirty.at(i) = false;
        }
    }

    [[nodiscard]] serialization::node export_state() const
    {
        assert(references.size() == originalValues.size());
        assert(std::all_of(modifiers.begin(), modifiers.end(),
            [](auto m) { return !m; }));
        serialization::node exportedState;
        auto value = originalValues.begin();
        for (const auto reference : references) {
            exportedState.put(std::to_string(reference), *value);
            ++value;
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
        lastModifierValues = originalValues;
        modifiers.assign(modifiedValues.size(), nullptr);
        modifierDirty.assign(modifiedValues.size(), false);
        indexMapping.swap(newMapping);
    }
};

template<typename T>
class set_variable_cache // NOLINT(bugprone-exception-escape): MSVC unordered-container moves may allocate.
{
public:
    void expose(value_reference reference, T startValue)
    {
        exposedVariables_.emplace(reference, exposed_variable{std::move(startValue), -1});
    }

    void set_value(value_reference reference,
        typename var_view_type<T>::type value)
    {
        assert(!hasRunModifiers_);
        const auto found = exposedVariables_.find(reference);
        if (found == exposedVariables_.end()) {
            std::ostringstream oss;
            oss << "Variable with value reference " << reference
                << " not found in exposed variables. Variables must be exposed "
                   "before calling set_value()";
            throw std::out_of_range(oss.str());
        }
        found->second.lastValue = value;
        if (!make_cache_slot(reference, found->second)) {
            assert(references_.at(found->second.arrayIndex) == reference);
            values_.at(found->second.arrayIndex) = value;
        }
    }

    void set_modifier(value_reference reference,
        std::function<T(T, duration)> modifier) // NOLINT(performance-unnecessary-value-param): Preserve the existing modifier copy behavior.
    {
        assert(!hasRunModifiers_);
        const auto found = exposedVariables_.find(reference);
        if (found == exposedVariables_.end()) {
            std::ostringstream oss;
            oss << "Variable with value reference " << reference
                << " not found in exposed variables. Variables must be exposed "
                   "before calling set_modifier()";
            throw std::out_of_range(oss.str());
        }
        make_cache_slot(reference, found->second);
        if (modifier) {
            modifiers_[reference] = modifier;
        } else {
            modifiers_.erase(reference);
        }
    }

    [[nodiscard]] bool has_modifier(value_reference reference) const
    {
        return modifiers_.count(reference) != 0;
    }

    // Whether `predicate` holds for every variable that has a modifier.
    template<typename Predicate>
    [[nodiscard]] bool all_modifiers_satisfy(const Predicate& predicate) const
    {
        return std::all_of(
            modifiers_.begin(), modifiers_.end(),
            [&](const auto& entry) { return predicate(entry.first); });
    }

    // Evaluates every modifier again on its variable's last raw value.
    [[nodiscard]] std::vector<std::pair<value_reference, T>>
    reevaluate_modifiers(duration deltaT) const
    {
        std::vector<std::pair<value_reference, T>> result;
        result.reserve(modifiers_.size());
        for (const auto& entry : modifiers_) {
            result.emplace_back(
                entry.first,
                entry.second(exposedVariables_.at(entry.first).lastValue, deltaT));
        }
        return result;
    }

    [[nodiscard]] bool contains(value_reference reference) const
    {
        return exposedVariables_.count(reference) != 0;
    }

    [[nodiscard]] bool
    last_value_equals(value_reference reference,
        typename var_view_type<T>::type value) const
    {
        return detail::event_values_equal(exposedVariables_.at(reference).lastValue,
            T(value));
    }

    std::pair<gsl::span<value_reference>, gsl::span<const T>> modify_and_get(
        duration deltaT,
        std::optional<std::function<bool(const value_reference, const T&)>>
            filter = std::nullopt,
        bool includeUnstagedModifiers = true,
        const std::function<bool(value_reference)>& replaySafe = {})
    {
        if (!hasRunModifiers_) {
            for (const auto& entry : modifiers_) {
                const auto ref = entry.first;
                auto& exposedVariable = exposedVariables_.at(ref);
                if (!includeUnstagedModifiers) {
                    if (exposedVariable.arrayIndex < 0) {
                        continue;
                    }
                    if (!replaySafe || !replaySafe(ref)) {
                        throw error(make_error_code(errc::unsupported_feature),
                            "A modified input cannot be changed again during "
                            "coupled event iteration");
                    }
                }
                make_cache_slot(ref, exposedVariable);
                values_.at(exposedVariable.arrayIndex) =
                    entry.second(values_.at(exposedVariable.arrayIndex), deltaT);
            }
            assert(references_.size() == values_.size());
            hasRunModifiers_ = true;
        }

        if (filter) {
            references_filtered_.clear();
            values_filtered_.clear();

            for (size_t i = 0; i < references_.size(); i++) {
                const auto& ref = references_.at(i);
                const auto& value = values_.at(i);

                if ((*filter)(ref, value)) {
                    references_filtered_.push_back(ref);
                    values_filtered_.push_back(value);
                }
            }

            return std::pair(gsl::make_span(references_filtered_),
                gsl::make_span(values_filtered_));
        }

        return std::pair(gsl::make_span(references_), gsl::make_span(values_));
    }

    void reset()
    {
        for (const auto ref : references_) {
            exposedVariables_.at(ref).arrayIndex = -1;
        }
        references_.clear();
        values_.clear();
        references_filtered_.clear();
        values_filtered_.clear();
        hasRunModifiers_ = false;
    }

    [[nodiscard]] serialization::node export_state() const
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
            variables.emplace(std::stoul(key),
                exposed_variable{child.template get_value<T>()});
        }
        exposedVariables_.swap(variables);
        hasRunModifiers_ = false;
        references_.clear();
        values_.clear();
    }

    [[nodiscard]] serialization::node export_state_v1() const
    {
        assert(modifiers_.empty());
        serialization::node exportedState;
        std::size_t index = 0;
        for (const auto& item : exposedVariables_) {
            serialization::node variable;
            variable.put("reference", item.first);
            variable.put("value", item.second.lastValue);
            variable.put("pending", item.second.arrayIndex >= 0);
            if (item.second.arrayIndex >= 0) {
                variable.put("pending_value", values_.at(static_cast<std::size_t>(item.second.arrayIndex)));
            }
            exportedState.put_child(std::to_string(index++), variable);
        }
        return exportedState;
    }

    void import_state_v1(const serialization::node& exportedState)
    {
        assert(modifiers_.empty());
        std::unordered_map<value_reference, exposed_variable> variables;
        std::vector<std::pair<value_reference, T>> pendingValues;
        for (std::size_t i = 0; i < exportedState.size(); ++i) {
            const auto& variable = exportedState.get_child(std::to_string(i));
            const auto reference =
                variable.template get<value_reference>("reference");
            variables.emplace(reference, exposed_variable{
                                             variable.template get<T>("value"),
                                         });
            if (variable.template get<bool>("pending")) {
                pendingValues.emplace_back(reference,
                    variable.template get<T>("pending_value"));
            }
        }
        exposedVariables_.swap(variables);
        hasRunModifiers_ = false;
        references_.clear();
        values_.clear();
        references_filtered_.clear();
        values_filtered_.clear();
        for (const auto& pending : pendingValues) {
            auto& exposed = exposedVariables_.at(pending.first);
            make_cache_slot(pending.first, exposed);
            values_.at(static_cast<std::size_t>(exposed.arrayIndex)) = pending.second;
        }
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

    // Filtered references and values of the values to be set next (if a filter is
    // applied).
    std::vector<value_reference> references_filtered_;
    boost::container::vector<T> values_filtered_;
};

// Copies the contents of a contiguous-storage container or span into another.
template<typename Src, typename Tgt>
void copy_contents(const Src& src, Tgt& tgt)
{
    assert(static_cast<std::size_t>(src.size()) <=
        static_cast<std::size_t>(tgt.size()));
    std::copy(src.begin(), src.end(), tgt.begin());
}

template<typename T>
T get_start_value(variable_description variable)
{
    if (!variable.start) {
        return T();
    }
    return std::get<T>(*variable.start);
}

std::uint64_t cache_key(variable_type type, value_reference reference)
{
    constexpr unsigned int type_shift = 32U;
    return (static_cast<std::uint64_t>(type) << type_shift) | reference;
}

template<typename T>
T scalar_start_or_default(const std::optional<scalar_value>& start)
{
    return start ? std::get<T>(*start) : T{};
}

variable_value scalar_variable_value(variable_type type,
    const std::optional<scalar_value>& start)
{
    switch (type) {
        case variable_type::real:
            return variable_value(
                real_values{{scalar_start_or_default<double>(start)}});
        case variable_type::integer:
            return variable_value(integer_values{{scalar_start_or_default<int>(start)}});
        case variable_type::boolean:
            return variable_value(
                boolean_values{{scalar_start_or_default<bool>(start)}});
        case variable_type::string:
            return variable_value(
                string_values{{scalar_start_or_default<std::string>(start)}});
        case variable_type::enumeration:
            return variable_value(enumeration_values{
                {scalar_start_or_default<enumeration_value>(start)},
            });
        case variable_type::float32:
            return variable_value(
                float32_values{{scalar_start_or_default<float>(start)}});
        case variable_type::int8:
            return variable_value(
                int8_values{{scalar_start_or_default<std::int8_t>(start)}});
        case variable_type::uint8:
            return variable_value(uint8_values{
                {scalar_start_or_default<std::uint8_t>(start)},
            });
        case variable_type::int16:
            return variable_value(int16_values{
                {scalar_start_or_default<std::int16_t>(start)},
            });
        case variable_type::uint16:
            return variable_value(uint16_values{
                {scalar_start_or_default<std::uint16_t>(start)},
            });
        case variable_type::uint32:
            return variable_value(uint32_values{
                {scalar_start_or_default<std::uint32_t>(start)},
            });
        case variable_type::int64:
            return variable_value(int64_values{
                {scalar_start_or_default<std::int64_t>(start)},
            });
        case variable_type::uint64:
            return variable_value(uint64_values{
                {scalar_start_or_default<std::uint64_t>(start)},
            });
        case variable_type::binary:
            return variable_value(binary_values{
                {scalar_start_or_default<binary_value>(start)},
            });
        case variable_type::clock:
            throw error(make_error_code(errc::unsupported_feature),
                "Clock variables cannot be exposed as simulation data");
    }
    COSIM_PANIC();
}

variable_value initial_variable_value(const variable_description& description)
{
    if (description.array_start) {
        return *description.array_start;
    }
    if (!description.dimensions.empty()) {
        throw error(make_error_code(errc::model_error),
            "Array variable '" + description.name +
                "' has no initial value");
    }
    return scalar_variable_value(description.type, description.start);
}

template<typename T>
std::enable_if_t<std::is_floating_point_v<T>, std::string>
format_exact_value(T value)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(std::numeric_limits<T>::max_digits10) << value;
    return stream.str();
}

template<typename T>
std::enable_if_t<std::is_integral_v<T> && std::is_signed_v<T>, std::string>
format_exact_value(T value)
{
    return std::to_string(static_cast<std::int64_t>(value));
}

template<typename T>
std::enable_if_t<std::is_integral_v<T> && !std::is_signed_v<T>, std::string>
format_exact_value(T value)
{
    return std::to_string(static_cast<std::uint64_t>(value));
}

template<typename T>
std::enable_if_t<std::is_floating_point_v<T>, T>
parse_exact_value(const std::string& text)
{
    std::istringstream stream(text);
    stream.imbue(std::locale::classic());
    T value;
    stream >> value;
    if (!stream || stream.peek() != std::char_traits<char>::eof()) {
        throw std::invalid_argument("Invalid floating-point value");
    }
    return value;
}

template<typename T>
std::enable_if_t<std::is_integral_v<T> && std::is_signed_v<T>, T>
parse_exact_value(const std::string& text)
{
    std::size_t parsed = 0;
    const auto value = std::stoll(text, &parsed);
    if (parsed != text.size() || value < (std::numeric_limits<T>::min)() || // NOLINT(readability-redundant-parentheses): Protect against Windows min/max macros.
        value > (std::numeric_limits<T>::max)()) { // NOLINT(readability-redundant-parentheses): Protect against Windows min/max macros.
        throw std::out_of_range("Signed integer value is out of range");
    }
    return static_cast<T>(value);
}

template<typename T>
std::enable_if_t<std::is_integral_v<T> && !std::is_signed_v<T>, T>
parse_exact_value(const std::string& text)
{
    if (!text.empty() && text.front() == '-') {
        throw std::out_of_range("Unsigned integer value is out of range");
    }
    std::size_t parsed = 0;
    const auto value = std::stoull(text, &parsed);
    if (parsed != text.size() || value > (std::numeric_limits<T>::max)()) { // NOLINT(readability-redundant-parentheses): Protect against Windows max macro.
        throw std::out_of_range("Unsigned integer value is out of range");
    }
    return static_cast<T>(value);
}

std::string encode_binary(const binary_value& value)
{
    static constexpr std::string_view digits = "0123456789abcdef";
    constexpr unsigned int digit_mask = 0xFU;
    std::string encoded;
    encoded.reserve(value.data.size() * 2);
    for (const auto byte : value.data) {
        encoded.push_back(
            digits.at((static_cast<unsigned int>(byte) >> 4U) & digit_mask));
        encoded.push_back(digits.at(byte & digit_mask));
    }
    return encoded;
}

binary_value decode_binary(const std::string& text)
{
    if (text.size() % 2 != 0) {
        throw std::invalid_argument("Invalid hexadecimal Binary value");
    }
    const auto digit = [](char character) -> std::uint8_t {
        constexpr int hex_letter_offset = 10;
        if (character >= '0' && character <= '9') {
            return static_cast<std::uint8_t>(character - '0');
        }
        if (character >= 'a' && character <= 'f') {
            return static_cast<std::uint8_t>(character - 'a' + hex_letter_offset);
        }
        throw std::invalid_argument("Invalid hexadecimal Binary value");
    };
    binary_value decoded;
    decoded.data.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        decoded.data.push_back(static_cast<std::uint8_t>(
            (static_cast<unsigned int>(digit(text.at(i))) << 4U) |
            digit(text.at(i + 1))));
    }
    return decoded;
}

template<typename Values, typename Formatter>
serialization::node export_values(const Values& values, Formatter formatter)
{
    serialization::node exported;
    for (std::size_t i = 0; i < values.values.size(); ++i) {
        exported.put(std::to_string(i), formatter(values.values.at(i)));
    }
    return exported;
}

template<typename Values, typename Parser>
Values import_values(const serialization::node& exported, Parser parser)
{
    Values values;
    values.values.reserve(exported.size());
    for (std::size_t i = 0; i < exported.size(); ++i) {
        values.values.push_back(
            parser(exported.get<std::string>(std::to_string(i))));
    }
    return values;
}

serialization::node export_variable_value(const variable_value& value)
{
    serialization::node exported;
    exported.put("type", static_cast<int>(value.type()));
    serialization::node dimensions;
    for (std::size_t i = 0; i < value.dimensions().size(); ++i) {
        dimensions.put(std::to_string(i),
            format_exact_value(value.dimensions().at(i)));
    }
    exported.put_child("dimensions", dimensions);

    const auto number = [](const auto& item) { return format_exact_value(item); };
    switch (value.type()) {
        case variable_type::real:
            exported.put_child(
                "values", export_values(std::get<real_values>(value.values()), number));
            break;
        case variable_type::integer:
            exported.put_child(
                "values",
                export_values(std::get<integer_values>(value.values()), number));
            break;
        case variable_type::boolean:
            exported.put_child(
                "values", export_values(std::get<boolean_values>(value.values()), [](bool item) { return item ? "1" : "0"; }));
            break;
        case variable_type::string:
            exported.put_child(
                "values", export_values(std::get<string_values>(value.values()), [](const std::string& item) { return item; }));
            break;
        case variable_type::enumeration:
            exported.put_child(
                "values", export_values(std::get<enumeration_values>(value.values()), [](enumeration_value item) {
                    return format_exact_value(item.value);
                }));
            break;
        case variable_type::float32:
            exported.put_child(
                "values",
                export_values(std::get<float32_values>(value.values()), number));
            break;
        case variable_type::int8:
            exported.put_child(
                "values", export_values(std::get<int8_values>(value.values()), number));
            break;
        case variable_type::uint8:
            exported.put_child(
                "values",
                export_values(std::get<uint8_values>(value.values()), number));
            break;
        case variable_type::int16:
            exported.put_child(
                "values",
                export_values(std::get<int16_values>(value.values()), number));
            break;
        case variable_type::uint16:
            exported.put_child(
                "values",
                export_values(std::get<uint16_values>(value.values()), number));
            break;
        case variable_type::uint32:
            exported.put_child(
                "values",
                export_values(std::get<uint32_values>(value.values()), number));
            break;
        case variable_type::int64:
            exported.put_child(
                "values",
                export_values(std::get<int64_values>(value.values()), number));
            break;
        case variable_type::uint64:
            exported.put_child(
                "values",
                export_values(std::get<uint64_values>(value.values()), number));
            break;
        case variable_type::binary:
            exported.put_child("values",
                export_values(std::get<binary_values>(value.values()),
                    [](const binary_value& item) {
                        return encode_binary(item);
                    }));
            break;
        case variable_type::clock:
            throw error(make_error_code(errc::unsupported_feature),
                "Clock values cannot be serialized as simulation data");
    }
    return exported;
}

variable_value import_variable_value(const serialization::node& exported)
{
    const auto rawType = exported.get<int>("type");
    if (rawType < static_cast<int>(variable_type::real) ||
        rawType > static_cast<int>(variable_type::clock)) {
        throw std::invalid_argument("Invalid variable type");
    }
    const auto type = static_cast<variable_type>(rawType);
    std::vector<std::uint64_t> dimensions;
    const auto& exportedDimensions = exported.get_child("dimensions");
    dimensions.reserve(exportedDimensions.size());
    for (std::size_t i = 0; i < exportedDimensions.size(); ++i) {
        dimensions.push_back(parse_exact_value<std::uint64_t>(
            exportedDimensions.get<std::string>(std::to_string(i))));
    }
    const auto& values = exported.get_child("values");
    switch (type) {
        case variable_type::real:
            return variable_value(
                import_values<real_values>(values, parse_exact_value<double>),
                dimensions);
        case variable_type::integer:
            return variable_value(
                import_values<integer_values>(values, parse_exact_value<int>),
                dimensions);
        case variable_type::boolean:
            return variable_value(
                import_values<boolean_values>(values,
                    [](const std::string& item) {
                        if (item == "0") {
                            return false;
                        }
                        if (item == "1") {
                            return true;
                        }
                        throw std::invalid_argument(
                            "Invalid Boolean value");
                    }),
                dimensions);
        case variable_type::string:
            return variable_value(
                import_values<string_values>(
                    values, [](const std::string& item) { return item; }),
                dimensions);
        case variable_type::enumeration:
            return variable_value(import_values<enumeration_values>(
                                      values,
                                      [](const std::string& item) {
                                          return enumeration_value{
                                              parse_exact_value<std::int64_t>(item),
                                          };
                                      }),
                dimensions);
        case variable_type::float32:
            return variable_value(
                import_values<float32_values>(values, parse_exact_value<float>),
                dimensions);
        case variable_type::int8:
            return variable_value(
                import_values<int8_values>(values, parse_exact_value<std::int8_t>),
                dimensions);
        case variable_type::uint8:
            return variable_value(
                import_values<uint8_values>(values, parse_exact_value<std::uint8_t>),
                dimensions);
        case variable_type::int16:
            return variable_value(
                import_values<int16_values>(values, parse_exact_value<std::int16_t>),
                dimensions);
        case variable_type::uint16:
            return variable_value(
                import_values<uint16_values>(values, parse_exact_value<std::uint16_t>),
                dimensions);
        case variable_type::uint32:
            return variable_value(
                import_values<uint32_values>(values, parse_exact_value<std::uint32_t>),
                dimensions);
        case variable_type::int64:
            return variable_value(
                import_values<int64_values>(values, parse_exact_value<std::int64_t>),
                dimensions);
        case variable_type::uint64:
            return variable_value(
                import_values<uint64_values>(values, parse_exact_value<std::uint64_t>),
                dimensions);
        case variable_type::binary:
            return variable_value(import_values<binary_values>(values, decode_binary),
                dimensions);
        case variable_type::clock:
            throw std::invalid_argument("Clock is not a simulation data value");
    }
    COSIM_PANIC();
}

bool uses_legacy_cache(const variable_description& description)
{
    if (!description.dimensions.empty() || !description.clocks.empty()) {
        return false;
    }
    return description.type == variable_type::real ||
        description.type == variable_type::integer ||
        description.type == variable_type::boolean ||
        description.type == variable_type::string;
}

template<typename Values>
typename Values::value_type
apply_scalar_modifier(const manipulable::variable_modifier& modifier,
    typename Values::value_type value, duration deltaT)
{
    const auto modified =
        modifier(variable_value(Values{{std::move(value)}}), deltaT);
    if (modified.type() != Values::type || !modified.is_scalar()) {
        throw error(
            make_error_code(errc::simulation_error),
            "A whole-variable modifier changed the scalar value type or shape");
    }
    return std::get<Values>(modified.values()).values.at(0);
}

struct whole_get_cache // NOLINT(bugprone-exception-escape): MSVC unordered-container moves may allocate.
{
    using modifier_type = manipulable::variable_modifier;

    void expose(typed_value_reference variable,
        const variable_description& description)
    {
        const auto key = cache_key(variable.type, variable.reference);
        if (indices.count(key) != 0U) {
            return;
        }
        indices.emplace(key, references.size());
        references.push_back(variable);
        values.emplace_back();
        rawValues.emplace_back();
        modifiers.emplace_back();
        modifierDirty.push_back(true);
        std::vector<std::uint64_t> fixedShape;
        for (const auto& dimension : description.dimensions) {
            const auto* const fixed = std::get_if<fixed_dimension>(&dimension);
            if (fixed == nullptr) {
                expectedDimensions.emplace_back();
                return;
            }
            fixedShape.push_back(fixed->size);
        }
        expectedDimensions.emplace_back(std::move(fixedShape));
    }

    [[nodiscard]] const variable_value&
    get(typed_value_reference variable) const
    {
        const auto index = indices.at(cache_key(variable.type, variable.reference));
        const auto& value = values.at(index);
        if (!value) {
            throw std::logic_error("Exposed variable has not been retrieved yet");
        }
        return value.value();
    }

    [[nodiscard]] const variable_value&
    get_original(typed_value_reference variable) const
    {
        const auto index = indices.at(cache_key(variable.type, variable.reference));
        const auto& value = rawValues.at(index);
        if (!value) {
            throw std::logic_error("Exposed variable has not been retrieved yet");
        }
        return value.value();
    }

    [[nodiscard]] bool contains(typed_value_reference variable) const
    {
        return indices.count(cache_key(variable.type, variable.reference)) != 0;
    }

    [[nodiscard]] bool has_modifier(typed_value_reference variable) const
    {
        const auto found =
            indices.find(cache_key(variable.type, variable.reference));
        return found != indices.end() &&
            static_cast<bool>(modifiers.at(found->second));
    }

    void update_one(typed_value_reference variable, variable_value retrieved,
        duration deltaT, bool reuseUnchanged = false)
    {
        const auto index = indices.at(cache_key(variable.type, variable.reference));
        const auto shape = retrieved.dimensions();
        if (retrieved.type() != references.at(index).type) {
            throw error(make_error_code(errc::model_error),
                "Slave returned a whole-variable value with the wrong type");
        }
        auto& expectedShape = expectedDimensions.at(index);
        if (expectedShape && expectedShape.value() != retrieved.dimensions()) {
            throw error(make_error_code(errc::simulation_error),
                "An exposed variable changed shape during simulation");
        }
        if (!expectedShape) {
            expectedShape = retrieved.dimensions();
        }
        const auto& lastRaw = rawValues.at(index);
        const bool reuse = reuseUnchanged && modifiers.at(index) &&
            !modifierDirty.at(index) && lastRaw &&
            values.at(index) &&
            detail::event_values_equal(lastRaw.value(), retrieved);
        rawValues.at(index) = retrieved;
        if (!reuse) {
            values.at(index) = modifiers.at(index)
                ? modifiers.at(index)(retrieved, deltaT)
                : std::move(retrieved);
        }
        modifierDirty.at(index) = false;
        auto& currentValue = values.at(index);
        if (!currentValue) {
            throw error(make_error_code(errc::simulation_error),
                "A whole-variable modifier did not produce a value");
        }
        if (const auto normalized =
                normalize_enumeration_value(variable.type, currentValue.value())) {
            currentValue = *normalized;
        }
        if (currentValue.value().type() != references.at(index).type ||
            currentValue.value().dimensions() != shape) {
            throw error(make_error_code(errc::simulation_error),
                "A whole-variable modifier changed the value type or shape");
        }
    }

    void update(std::vector<variable_value> retrieved, duration deltaT,
        bool reuseUnchanged = false)
    {
        if (retrieved.size() != references.size()) {
            throw error(
                make_error_code(errc::model_error),
                "Slave returned an incorrect number of whole-variable values");
        }
        for (std::size_t i = 0; i < retrieved.size(); ++i) {
            update_one(references.at(i), std::move(retrieved.at(i)), deltaT,
                reuseUnchanged);
        }
    }

    void set_modifier(typed_value_reference variable, modifier_type modifier)
    {
        const auto index = indices.at(cache_key(variable.type, variable.reference));
        modifiers.at(index) = std::move(modifier);
        modifierDirty.at(index) = true;
    }

    [[nodiscard]] bool has_modifiers() const
    {
        return boost::algorithm::any_of(
            modifiers, [](const auto& value) { return static_cast<bool>(value); });
    }

    [[nodiscard]] serialization::node export_state() const
    {
        assert(!has_modifiers());
        serialization::node exported;
        for (std::size_t i = 0; i < references.size(); ++i) {
            serialization::node entry;
            const auto& expectedShape = expectedDimensions.at(i);
            const auto& value = values.at(i);
            entry.put("type", static_cast<int>(references.at(i).type));
            entry.put("reference", references.at(i).reference);
            entry.put("has_value", value.has_value());
            entry.put("has_expected_dimensions", expectedShape.has_value());
            if (expectedShape) {
                serialization::node dimensions;
                const auto& shape = expectedShape.value();
                for (std::size_t dimension = 0; dimension < shape.size(); ++dimension) {
                    dimensions.put(std::to_string(dimension),
                        format_exact_value(shape.at(dimension)));
                }
                entry.put_child("expected_dimensions", dimensions);
            }
            if (value) {
                entry.put_child("value", export_variable_value(value.value()));
            }
            exported.put_child(std::to_string(i), entry);
        }
        return exported;
    }

    void import_state(const serialization::node& exported)
    {
        references.clear();
        values.clear();
        rawValues.clear();
        modifiers.clear();
        modifierDirty.clear();
        expectedDimensions.clear();
        indices.clear();
        references.reserve(exported.size());
        values.reserve(exported.size());
        rawValues.reserve(exported.size());
        modifiers.reserve(exported.size());
        modifierDirty.reserve(exported.size());
        expectedDimensions.reserve(exported.size());
        for (std::size_t i = 0; i < exported.size(); ++i) {
            const auto& entry = exported.get_child(std::to_string(i));
            const auto rawType = entry.get<int>("type");
            if (rawType < static_cast<int>(variable_type::real) ||
                rawType >= static_cast<int>(variable_type::clock)) {
                throw std::invalid_argument("Invalid cached variable type");
            }
            const auto type = static_cast<variable_type>(rawType);
            const typed_value_reference variable{
                type,
                entry.get<value_reference>("reference"),
            };
            std::optional<variable_value> value;
            if (entry.get<bool>("has_value")) {
                value = import_variable_value(entry.get_child("value"));
                if (value->type() != type) {
                    throw std::invalid_argument(
                        "Cached variable type does not match its value");
                }
            }
            indices.emplace(cache_key(type, variable.reference), references.size());
            references.push_back(variable);
            values.push_back(std::move(value));
            rawValues.push_back(values.back());
            modifiers.emplace_back();
            modifierDirty.push_back(false);
            if (entry.get<bool>("has_expected_dimensions")) {
                std::vector<std::uint64_t> dimensions;
                const auto& serialized = entry.get_child("expected_dimensions");
                dimensions.reserve(serialized.size());
                for (std::size_t dimension = 0; dimension < serialized.size();
                    ++dimension) {
                    dimensions.push_back(parse_exact_value<std::uint64_t>(
                        serialized.get<std::string>(std::to_string(dimension))));
                }
                expectedDimensions.emplace_back(std::move(dimensions));
            } else {
                expectedDimensions.emplace_back();
            }
            const auto& cachedValue = values.back();
            const auto& expectedShape = expectedDimensions.back();
            if (cachedValue && expectedShape &&
                cachedValue.value().dimensions() != expectedShape.value()) {
                throw std::invalid_argument(
                    "Cached variable shape does not match its expected shape");
            }
        }
    }

    std::vector<typed_value_reference> references;
    std::vector<std::optional<variable_value>> values;
    std::vector<std::optional<variable_value>> rawValues;
    std::vector<modifier_type> modifiers;
    std::vector<bool> modifierDirty;
    std::vector<std::optional<std::vector<std::uint64_t>>> expectedDimensions;
    std::unordered_map<std::uint64_t, std::size_t> indices;
};

struct whole_set_cache // NOLINT(bugprone-exception-escape): MSVC unordered-container moves may allocate.
{
    using modifier_type = manipulable::variable_modifier;

    void expose(typed_value_reference variable,
        const variable_description& description)
    {
        const auto key = cache_key(variable.type, variable.reference);
        if (indices.count(key) != 0U) {
            return;
        }
        indices.emplace(key, references.size());
        references.push_back(variable);
        if (description.type == variable_type::enumeration &&
            description.dimensions.empty() && !description.start) {
            values.emplace_back(std::nullopt);
        } else {
            values.emplace_back(initial_variable_value(description));
        }
        pending.push_back(false);
        modifiers.emplace_back();
    }

    void set(typed_value_reference variable, const variable_value& value)
    {
        const auto index = indices.at(cache_key(variable.type, variable.reference));
        auto& currentValue = values.at(index);
        if (value.type() != variable.type ||
            (currentValue ? value.dimensions() != currentValue.value().dimensions()
                          : !value.is_scalar())) {
            throw std::invalid_argument(
                "Whole-variable assignment type or shape mismatch");
        }
        currentValue = value;
        pending.at(index) = true;
    }

    [[nodiscard]] bool contains(typed_value_reference variable) const
    {
        return indices.count(cache_key(variable.type, variable.reference)) != 0;
    }

    [[nodiscard]] bool has_modifier(typed_value_reference variable) const
    {
        const auto found =
            indices.find(cache_key(variable.type, variable.reference));
        return found != indices.end() &&
            static_cast<bool>(modifiers.at(found->second));
    }

    [[nodiscard]] bool needs_application(typed_value_reference variable) const
    {
        const auto index = indices.at(cache_key(variable.type, variable.reference));
        return pending.at(index) || static_cast<bool>(modifiers.at(index));
    }

    [[nodiscard]] bool last_value_equals(typed_value_reference variable,
        const variable_value& value) const
    {
        const auto index = indices.at(cache_key(variable.type, variable.reference));
        const auto& currentValue = values.at(index);
        return currentValue &&
            detail::event_values_equal(currentValue.value(), value);
    }

    [[nodiscard]] const variable_value& checked_value(std::size_t index) const
    {
        const auto& currentValue = values.at(index);
        if (!currentValue) {
            throw error(
                make_error_code(errc::simulation_error),
                "Cannot modify an Enumeration variable without an initial value");
        }
        return currentValue.value();
    }

    variable_value modify_one(typed_value_reference variable, duration deltaT)
    {
        const auto index = indices.at(cache_key(variable.type, variable.reference));
        if (!values.at(index)) {
            throw error(
                make_error_code(errc::simulation_error),
                "Cannot modify an Enumeration variable without an initial value");
        }
        auto value = modifiers.at(index)
            ? modifiers.at(index)(checked_value(index), deltaT)
            : checked_value(index);
        if (const auto normalized =
                normalize_enumeration_value(variable.type, value)) {
            value = *normalized;
        }
        if (value.type() != references.at(index).type ||
            value.dimensions() != checked_value(index).dimensions()) {
            throw error(make_error_code(errc::simulation_error),
                "A whole-variable modifier changed the value type or shape");
        }
        pending.at(index) = false;
        return value;
    }

    std::pair<std::vector<typed_value_reference>, std::vector<variable_value>>
    modify_and_get(
        duration deltaT,
        std::optional<
            std::function<bool(typed_value_reference, const variable_value&)>>
            filter = std::nullopt,
        bool includeUnstagedModifiers = true,
        const std::function<bool(typed_value_reference)>& replaySafe = {})
    {
        std::vector<typed_value_reference> selectedReferences;
        std::vector<variable_value> selectedValues;
        for (std::size_t i = 0; i < references.size(); ++i) {
            if (!pending.at(i) && (!includeUnstagedModifiers || !modifiers.at(i))) {
                continue;
            }
            if (!includeUnstagedModifiers && pending.at(i) && modifiers.at(i) &&
                (!replaySafe || !replaySafe(references.at(i)))) {
                throw error(make_error_code(errc::unsupported_feature),
                    "A modified input cannot be changed again during coupled "
                    "event iteration");
            }
            if (!values.at(i)) {
                throw error(
                    make_error_code(errc::simulation_error),
                    "Cannot modify an Enumeration variable without an initial value");
            }
            if (filter && !(*filter)(references.at(i), checked_value(i))) {
                continue;
            }
            auto value = modifiers.at(i) ? modifiers.at(i)(checked_value(i), deltaT)
                                         : checked_value(i);
            if (const auto normalized =
                    normalize_enumeration_value(references.at(i).type, value)) {
                value = *normalized;
            }
            if (value.type() != references.at(i).type ||
                value.dimensions() != checked_value(i).dimensions()) {
                throw error(
                    make_error_code(errc::simulation_error),
                    "A whole-variable modifier changed the value type or shape");
            }
            selectedReferences.push_back(references.at(i));
            selectedValues.push_back(std::move(value));
            pending.at(i) = false;
        }
        return {std::move(selectedReferences), std::move(selectedValues)};
    }

    // Whether `predicate` holds for every variable that has a modifier.
    template<typename Predicate>
    [[nodiscard]] bool all_modifiers_satisfy(const Predicate& predicate) const
    {
        for (std::size_t i = 0; i < references.size(); ++i) {
            if (modifiers.at(i) && !predicate(references.at(i))) {
                return false;
            }
        }
        return true;
    }

    // Evaluates every modifier again on its variable's last raw value.
    [[nodiscard]] std::vector<std::pair<typed_value_reference, variable_value>>
    reevaluate_modifiers(duration deltaT) const
    {
        std::vector<std::pair<typed_value_reference, variable_value>> result;
        for (std::size_t i = 0; i < references.size(); ++i) {
            if (!modifiers.at(i)) {
                continue;
            }
            if (!values.at(i)) {
                throw error(
                    make_error_code(errc::simulation_error),
                    "Cannot modify an Enumeration variable without an initial value");
            }
            auto value = modifiers.at(i)(checked_value(i), deltaT);
            if (const auto normalized =
                    normalize_enumeration_value(references.at(i).type, value)) {
                value = *normalized;
            }
            if (value.type() != references.at(i).type ||
                value.dimensions() != checked_value(i).dimensions()) {
                throw error(
                    make_error_code(errc::simulation_error),
                    "A whole-variable modifier changed the value type or shape");
            }
            result.emplace_back(references.at(i), std::move(value));
        }
        return result;
    }

    void set_modifier(typed_value_reference variable, modifier_type modifier)
    {
        const auto index = indices.at(cache_key(variable.type, variable.reference));
        if (modifiers.at(index) && !modifier && values.at(index)) {
            pending.at(index) = true;
        }
        modifiers.at(index) = std::move(modifier);
    }

    [[nodiscard]] bool has_modifiers() const
    {
        return boost::algorithm::any_of(
            modifiers, [](const auto& value) { return static_cast<bool>(value); });
    }

    [[nodiscard]] serialization::node export_state() const
    {
        assert(!has_modifiers());
        serialization::node exported;
        for (std::size_t i = 0; i < references.size(); ++i) {
            serialization::node entry;
            entry.put("type", static_cast<int>(references.at(i).type));
            entry.put("reference", references.at(i).reference);
            entry.put("pending", pending.at(i));
            const auto& currentValue = values.at(i);
            entry.put("has_value", currentValue.has_value());
            if (currentValue) {
                entry.put_child("value", export_variable_value(currentValue.value()));
            }
            exported.put_child(std::to_string(i), entry);
        }
        return exported;
    }

    void import_state(const serialization::node& exported)
    {
        references.clear();
        values.clear();
        pending.clear();
        modifiers.clear();
        indices.clear();
        references.reserve(exported.size());
        values.reserve(exported.size());
        pending.reserve(exported.size());
        modifiers.reserve(exported.size());
        for (std::size_t i = 0; i < exported.size(); ++i) {
            const auto& entry = exported.get_child(std::to_string(i));
            const auto rawType = entry.get<int>("type");
            if (rawType < static_cast<int>(variable_type::real) ||
                rawType >= static_cast<int>(variable_type::clock)) {
                throw std::invalid_argument("Invalid cached variable type");
            }
            const auto type = static_cast<variable_type>(rawType);
            const typed_value_reference variable{
                type,
                entry.get<value_reference>("reference"),
            };
            const auto hasValue = entry.get<bool>("has_value", true);
            std::optional<variable_value> value;
            if (hasValue) {
                value = import_variable_value(entry.get_child("value"));
            }
            if ((value && value->type() != type) ||
                (!value && type != variable_type::enumeration)) {
                throw std::invalid_argument(
                    "Cached variable type does not match its value");
            }
            indices.emplace(cache_key(type, variable.reference), references.size());
            references.push_back(variable);
            values.push_back(std::move(value));
            pending.push_back(entry.get<bool>("pending"));
            if (!values.back() && pending.back()) {
                throw std::invalid_argument("Pending cached variable has no value");
            }
            modifiers.emplace_back();
        }
    }

    std::vector<typed_value_reference> references;
    std::vector<std::optional<variable_value>> values;
    std::vector<bool> pending;
    std::vector<modifier_type> modifiers;
    std::unordered_map<std::uint64_t, std::size_t> indices;
};

template<typename Values, typename Modifier>
manipulable::variable_modifier wrap_clocked_scalar_modifier(Modifier modifier)
{
    if (!modifier) {
        return {};
    }
    return [modifier = std::move(modifier)](const variable_value& value,
               duration deltaT) {
        return variable_value(Values{
            {modifier(std::get<Values>(value.values()).values.at(0), deltaT)},
        });
    };
}
} // namespace

class slave_simulator::impl : public clock_controller
{
    struct state;

public:
    impl(std::shared_ptr<slave> slave, std::string_view name)
        : slave_(std::move(slave))
        , name_(name)
        , modelDescription_(slave_->model_description())
        , clockController_(slave_->get_clock_controller())
        , eventController_(slave_->get_event_controller())
    {
        assert(slave_);
        assert(!name_.empty());
    }

    ~impl() noexcept override = default;

    impl(const impl&) = delete;

    impl& operator=(const impl&) = delete;

    impl(impl&&) noexcept = delete;

    impl& operator=(impl&&) noexcept = delete;

    [[nodiscard]] std::string name() const { return name_; }

    [[nodiscard]] cosim::model_description model_description() const
    {
        return modelDescription_;
    }

    [[nodiscard]] std::shared_ptr<slave> get_slave() const { return slave_; }

    clock_controller* get_clock_controller() noexcept
    {
        return (clockController_ != nullptr) ? this : nullptr;
    }

    event_controller* get_event_controller() noexcept
    {
        return (eventController_ != nullptr) ? this : nullptr;
    }

    void set_max_event_iterations(std::size_t limit)
    {
        slave_->set_max_event_iterations(limit);
    }

    void signal_input_discontinuity(variable_type type, value_reference ref)
    {
        const auto& description = find_variable_description(ref, type);
        const bool exposed =
            uses_legacy_cache(description)
            ? (type == variable_type::real && state_.realSetCache.contains(ref))
            : state_.wholeSetCache.contains({type, ref});
        if ((eventController_ == nullptr) || !simulationStarted_) {
            throw error(make_error_code(errc::unsupported_feature),
                name_ + ": input discontinuities require an active Event "
                        "Mode capability");
        }
        if (!exposed || description.causality != variable_causality::input ||
            description.variability != variable_variability::continuous ||
            !description.clocks.empty() ||
            (type != variable_type::real && type != variable_type::float32)) {
            throw error(make_error_code(errc::invalid_system_structure),
                name_ + ": a discontinuity must identify an exposed "
                        "non-clocked continuous input");
        }
        inputDiscontinuities_.emplace(type, ref);
    }

    void set_input_sample_discontinuity_policy(variable_type type,
        value_reference ref,
        bool enabled)
    {
        const auto& description = find_variable_description(ref, type);
        const bool exposed =
            uses_legacy_cache(description)
            ? (type == variable_type::real && state_.realSetCache.contains(ref))
            : state_.wholeSetCache.contains({type, ref});
        if ((eventController_ == nullptr) || simulationStarted_) {
            throw error(make_error_code(errc::unsupported_feature),
                name_ + ": sample discontinuity policies require Event Mode "
                        "and must be configured before simulation starts");
        }
        if (!exposed || description.causality != variable_causality::input ||
            description.variability != variable_variability::continuous ||
            !description.clocks.empty() ||
            (type != variable_type::real && type != variable_type::float32)) {
            throw error(make_error_code(errc::invalid_system_structure),
                name_ + ": a sample discontinuity policy requires an exposed "
                        "non-clocked continuous input");
        }
        if (enabled) {
            state_.sampleDiscontinuityInputs.emplace(type, ref);
        } else {
            state_.sampleDiscontinuityInputs.erase({type, ref});
        }
    }

    void set_input_modifier_replay_safe(variable_type type, value_reference ref,
        bool enabled)
    {
        const auto& description = find_variable_description(ref, type);
        if ((eventController_ == nullptr) || inputsPrepared_ ||
            (simulationStarted_ && eventController_->is_in_event_mode())) {
            throw error(make_error_code(errc::unsupported_feature),
                name_ + ": modifier replay safety must be configured outside "
                        "event coordination");
        }
        if (!description.clocks.empty()) {
            throw error(make_error_code(errc::unsupported_feature),
                name_ + ": replay safety for Clock-associated input "
                        "modifiers is not supported");
        }
        if (description.causality != variable_causality::input &&
            (description.causality != variable_causality::parameter ||
                description.variability != variable_variability::tunable)) {
            throw error(make_error_code(errc::invalid_system_structure),
                name_ +
                    ": replay safety requires an input or tunable parameter");
        }
        bool hasModifier = false;
        bool exposed = false;
        if (uses_legacy_cache(description)) {
            switch (type) {
                case variable_type::real:
                    exposed = state_.realSetCache.contains(ref);
                    hasModifier = state_.realSetCache.has_modifier(ref);
                    break;
                case variable_type::integer:
                    exposed = state_.integerSetCache.contains(ref);
                    hasModifier = state_.integerSetCache.has_modifier(ref);
                    break;
                case variable_type::boolean:
                    exposed = state_.booleanSetCache.contains(ref);
                    hasModifier = state_.booleanSetCache.has_modifier(ref);
                    break;
                case variable_type::string:
                    exposed = state_.stringSetCache.contains(ref);
                    hasModifier = state_.stringSetCache.has_modifier(ref);
                    break;
                default:
                    COSIM_PANIC();
            }
        } else {
            exposed = state_.wholeSetCache.contains({type, ref});
            hasModifier = state_.wholeSetCache.has_modifier({type, ref});
        }
        if (!exposed) {
            throw error(make_error_code(errc::invalid_system_structure),
                name_ + ": replay safety requires an exposed input");
        }
        if (enabled && !hasModifier) {
            throw error(make_error_code(errc::invalid_system_structure),
                name_ +
                    ": replay safety requires an installed input modifier");
        }
        if (enabled) {
            replaySafeInputs_.emplace(type, ref);
        } else {
            replaySafeInputs_.erase({type, ref});
        }
    }

    void begin_input_event_coordination(time_point currentTime)
    {
        if (!state_.deferredAt || currentTime <= *state_.deferredAt) {
            return;
        }
        for (auto& entry : state_.deferredInputs) {
            const auto existing = boost::range::find_if(
                preparedInputs_, [&entry](const auto& candidate) {
                    return candidate.first == entry.first;
                });
            if (existing == preparedInputs_.end()) {
                preparedInputs_.push_back(std::move(entry));
            }
        }
        inputDiscontinuities_.insert(state_.deferredDiscontinuities.begin(),
            state_.deferredDiscontinuities.end());
        state_.deferredInputs.clear();
        state_.deferredDiscontinuities.clear();
        state_.deferredAt.reset();
    }

    void defer_input_events(time_point currentTime)
    {
        for (auto it = preparedInputs_.begin(); it != preparedInputs_.end();) {
            const auto& description =
                find_variable_description(it->first.reference, it->first.type);
            const bool eventInput =
                (description.causality == variable_causality::input &&
                    description.variability == variable_variability::discrete) ||
                (description.causality == variable_causality::parameter &&
                    description.variability == variable_variability::tunable) ||
                inputDiscontinuities_.count({it->first.type, it->first.reference}) !=
                    0;
            if (!eventInput) {
                ++it;
                continue;
            }
            const auto existing = boost::range::find_if(
                state_.deferredInputs,
                [&it](const auto& entry) { return entry.first == it->first; });
            if (existing == state_.deferredInputs.end()) {
                state_.deferredInputs.push_back(std::move(*it));
            } else {
                existing->second = std::move(it->second);
            }
            it = preparedInputs_.erase(it);
        }
        state_.deferredDiscontinuities.insert(inputDiscontinuities_.begin(),
            inputDiscontinuities_.end());
        inputDiscontinuities_.clear();
        changedEventInputs_.clear();
        inputEventPrepared_ = false;
        state_.deferredAt = currentTime;
    }

    bool prepare_input_events(duration deltaT, bool evaluateUnstagedModifiers)
    {
        if (eventController_ == nullptr) {
            return false;
        }
        const bool firstPreparation = !inputsPrepared_ && evaluateUnstagedModifiers;
        const auto replaySafe = [this](typed_value_reference variable) {
            return replaySafeInputs_.count({variable.type, variable.reference}) != 0;
        };
        const auto [realRefs, realValues] = state_.realSetCache.modify_and_get(
            deltaT, std::nullopt, firstPreparation,
            [&replaySafe](value_reference ref) {
                return replaySafe({variable_type::real, ref});
            });
        const auto [integerRefs, integerValues] =
            state_.integerSetCache.modify_and_get(
                deltaT, std::nullopt, firstPreparation,
                [&replaySafe](value_reference ref) {
                    return replaySafe({variable_type::integer, ref});
                });
        const auto [booleanRefs, booleanValues] =
            state_.booleanSetCache.modify_and_get(
                deltaT, std::nullopt, firstPreparation,
                [&replaySafe](value_reference ref) {
                    return replaySafe({variable_type::boolean, ref});
                });
        const auto [stringRefs, stringValues] =
            state_.stringSetCache.modify_and_get(
                deltaT, std::nullopt, firstPreparation,
                [&replaySafe](value_reference ref) {
                    return replaySafe({variable_type::string, ref});
                });
        const auto [wholeRefs, wholeValues] = state_.wholeSetCache.modify_and_get(
            deltaT, std::nullopt, firstPreparation, replaySafe);

        const auto stage = [this](typed_value_reference variable,
                               variable_value value) {
            stage_prepared_input(variable, std::move(value));
        };
        auto realValue = realValues.begin();
        for (const auto reference : realRefs) {
            stage({variable_type::real, reference},
                variable_value(real_values{{*realValue++}}));
        }
        auto integerValue = integerValues.begin();
        for (const auto reference : integerRefs) {
            stage({variable_type::integer, reference},
                variable_value(integer_values{{*integerValue++}}));
        }
        auto booleanValue = booleanValues.begin();
        for (const auto reference : booleanRefs) {
            stage({variable_type::boolean, reference},
                variable_value(boolean_values{{*booleanValue++}}));
        }
        auto stringValue = stringValues.begin();
        for (const auto reference : stringRefs) {
            stage({variable_type::string, reference},
                variable_value(string_values{{*stringValue++}}));
        }
        auto wholeValue = wholeValues.begin();
        for (const auto reference : wholeRefs) {
            stage(reference, *wholeValue++);
        }
        state_.realSetCache.reset();
        state_.integerSetCache.reset();
        state_.booleanSetCache.reset();
        state_.stringSetCache.reset();
        inputsPrepared_ = true;

        std::vector<typed_value_reference> candidates;
        std::vector<variable_value> proposed;
        for (const auto& entry : preparedInputs_) {
            const auto& description =
                find_variable_description(entry.first.reference, entry.first.type);
            if ((description.causality == variable_causality::input &&
                    description.variability == variable_variability::discrete) ||
                (description.causality == variable_causality::parameter &&
                    description.variability == variable_variability::tunable) ||
                state_.sampleDiscontinuityInputs.count(
                    {entry.first.type, entry.first.reference}) != 0) {
                candidates.push_back(entry.first);
                proposed.push_back(entry.second);
            }
        }
        const auto current =
            candidates.empty()
            ? std::vector<variable_value>{}
            : slave_->get_variable_values(gsl::make_span(candidates));
        if (current.size() != candidates.size()) {
            throw error(make_error_code(errc::model_error),
                name_ +
                    ": incorrect input value count when preparing events");
        }
        inputEventPrepared_ = !inputDiscontinuities_.empty();
        changedEventInputs_.clear();
        for (std::size_t i = 0; i < candidates.size(); ++i) {
            if (!detail::event_values_equal(current.at(i), proposed.at(i))) {
                const auto& candidate = candidates.at(i);
                changedEventInputs_.emplace(candidate.type, candidate.reference);
                if (state_.sampleDiscontinuityInputs.count(
                        {candidate.type, candidate.reference}) != 0) {
                    inputDiscontinuities_.emplace(candidate.type, candidate.reference);
                }
                inputEventPrepared_ = true;
            }
        }
        return inputEventPrepared_;
    }

    void stage_prepared_input(typed_value_reference variable,
        variable_value value)
    {
        const auto existing =
            boost::range::find_if(preparedInputs_, [variable](const auto& entry) {
                return entry.first == variable;
            });
        if (existing == preparedInputs_.end()) {
            preparedInputs_.emplace_back(variable, std::move(value));
        } else {
            existing->second = std::move(value);
        }
    }

    bool reevaluate_input_modifiers(duration deltaT)
    {
        if ((eventController_ == nullptr) || !inputsPrepared_) {
            return false;
        }
        const auto replaySafe = [this](typed_value_reference variable) {
            return replaySafeInputs_.count({variable.type, variable.reference}) != 0;
        };
        const auto scalarSafe = [&replaySafe](variable_type type) {
            return [&replaySafe, type](value_reference ref) {
                return replaySafe({type, ref});
            };
        };
        if (!state_.realSetCache.all_modifiers_satisfy(
                scalarSafe(variable_type::real)) ||
            !state_.integerSetCache.all_modifiers_satisfy(
                scalarSafe(variable_type::integer)) ||
            !state_.booleanSetCache.all_modifiers_satisfy(
                scalarSafe(variable_type::boolean)) ||
            !state_.stringSetCache.all_modifiers_satisfy(
                scalarSafe(variable_type::string)) ||
            !state_.wholeSetCache.all_modifiers_satisfy(replaySafe)) {
            throw error(
                make_error_code(errc::unsupported_feature),
                name_ + ": an input event changed the step size after modifiers were "
                        "evaluated, and a modified input is not replay-safe");
        }
        bool reevaluated = false;
        const auto restage = [&](variable_type type, const auto& cache,
                                 auto makeValue) {
            for (const auto& entry : cache.reevaluate_modifiers(deltaT)) {
                stage_prepared_input({type, entry.first}, makeValue(entry.second));
                reevaluated = true;
            }
        };
        restage(variable_type::real, state_.realSetCache,
            [](double value) { return variable_value(real_values{{value}}); });
        restage(variable_type::integer, state_.integerSetCache,
            [](int value) { return variable_value(integer_values{{value}}); });
        restage(variable_type::boolean, state_.booleanSetCache,
            [](bool value) { return variable_value(boolean_values{{value}}); });
        restage(variable_type::string, state_.stringSetCache,
            [](const std::string& value) {
                return variable_value(string_values{{value}});
            });
        for (auto& entry : state_.wholeSetCache.reevaluate_modifiers(deltaT)) {
            stage_prepared_input(entry.first, std::move(entry.second));
            reevaluated = true;
        }
        return reevaluated;
    }

    void apply_event_inputs(bool coupled = false)
    {
        if (!inputsPrepared_ || (eventController_ == nullptr) ||
            !eventController_->is_in_event_mode()) {
            throw error(make_error_code(errc::simulation_error),
                name_ +
                    ": event inputs require prepared values and Event Mode");
        }
        if (coupled) {
            std::vector<typed_value_reference> references;
            std::vector<variable_value> values;
            for (const auto& entry : preparedInputs_) {
                const auto key =
                    std::make_pair(entry.first.type, entry.first.reference);
                if (restrictedEventSets_.count(key) != 0) {
                    continue;
                }
                references.push_back(entry.first);
                values.push_back(entry.second);
                if (!find_variable_description(entry.first.reference, entry.first.type)
                        .can_handle_multiple_set_per_time_instant.value_or(true)) {
                    restrictedEventSets_.insert(key);
                }
            }
            if (!references.empty()) {
                slave_->set_variable_values(gsl::make_span(references),
                    gsl::make_span(values));
            }
            const auto delivered = [&references](const auto& entry) {
                return std::find(references.begin(), references.end(), entry.first) !=
                    references.end();
            };
            preparedInputs_.erase(boost::range::remove_if(preparedInputs_, delivered),
                preparedInputs_.end());
            const auto stillStaged = [&](const auto& key) {
                return std::any_of(preparedInputs_.begin(), preparedInputs_.end(),
                    [&](const auto& entry) {
                        return key ==
                            std::make_pair(entry.first.type,
                                entry.first.reference);
                    });
            };
            for (auto it = inputDiscontinuities_.begin();
                it != inputDiscontinuities_.end();) {
                if (stillStaged(*it)) {
                    ++it;
                } else {
                    it = inputDiscontinuities_.erase(it);
                }
            }
            for (const auto& ref : references) {
                changedEventInputs_.erase({ref.type, ref.reference});
            }
            inputEventPrepared_ =
                !changedEventInputs_.empty() || !inputDiscontinuities_.empty();
        } else {
            apply_prepared_inputs();
            inputDiscontinuities_.clear();
            changedEventInputs_.clear();
            inputEventPrepared_ = false;
        }
    }

    [[nodiscard]] bool has_applicable_event_inputs() const
    {
        const auto applicable = [this](const auto& key) {
            return restrictedEventSets_.count(key) == 0;
        };
        return boost::algorithm::any_of(changedEventInputs_, applicable) ||
            boost::algorithm::any_of(inputDiscontinuities_, applicable);
    }

    void finish_input_event_coordination()
    {
        if (inputEventPrepared_ || !inputDiscontinuities_.empty()) {
            throw error(make_error_code(errc::simulation_error),
                name_ +
                    ": cannot finish coordination with pending input events");
        }
        inputsPrepared_ = false;
        const auto remaining = std::move(preparedInputs_);
        preparedInputs_.clear();
        for (const auto& entry : remaining) {
            set_variable_value(entry.first.type, entry.first.reference, entry.second);
        }
    }

    void refresh_event_values() { get_variables(duration::zero(), false, true); }

    [[nodiscard]] variable_value
    get_event_output_value(variable_type type, value_reference ref) const
    {
        const auto& description = find_variable_description(ref, type);
        if (!description.clocks.empty()) {
            return state_.clockedGetCache.get_original({type, ref});
        }
        if (!uses_legacy_cache(description)) {
            return state_.wholeGetCache.get_original({type, ref});
        }
        switch (type) {
            case variable_type::real:
                return variable_value(
                    real_values{{state_.realGetCache.get_original(ref)}});
            case variable_type::integer:
                return variable_value(
                    integer_values{{state_.integerGetCache.get_original(ref)}});
            case variable_type::boolean:
                return variable_value(
                    boolean_values{{state_.booleanGetCache.get_original(ref)}});
            case variable_type::string:
                return variable_value(string_values{
                    {std::string(state_.stringGetCache.get_original(ref))},
                });
            default:
                COSIM_PANIC();
        }
    }

    [[nodiscard]] bool is_in_event_mode() const noexcept override
    {
        return (eventController_ != nullptr) ? eventController_->is_in_event_mode()
                                             : clockController_->is_in_event_mode();
    }

    void enter_event_mode() override
    {
        restrictedEventSets_.clear();
        if (eventController_ != nullptr) {
            eventController_->enter_event_mode();
        } else {
            clockController_->enter_event_mode();
        }
    }

    [[nodiscard]] std::optional<clock_description>
    resolved_input_clock_schedule(value_reference reference) const override
    {
        return clockController_->resolved_input_clock_schedule(reference);
    }

    clock_interval_update
    get_input_clock_interval(value_reference reference) override
    {
        return clockController_->get_input_clock_interval(reference);
    }

    [[nodiscard]] bool has_pending_clock_intervals() const override
    {
        return clockController_->has_pending_clock_intervals();
    }

    [[nodiscard]] std::optional<clock_interval_update>
    pending_input_clock_interval(value_reference reference) const override
    {
        return clockController_->pending_input_clock_interval(reference);
    }

    void apply_pending_clock_intervals() override
    {
        clockController_->apply_pending_clock_intervals();
    }

    bool get_output_clock(value_reference reference) override
    {
        const auto active = clockController_->get_output_clock(reference);
        if (active) {
            refresh_clocked_values(reference);
        }
        return active;
    }

    bool probe_output_clock(value_reference reference) override
    {
        return clockController_->get_output_clock(reference);
    }

    void set_input_clock(value_reference reference, bool active) override
    {
        clockController_->set_input_clock(reference, active);
        if (active) {
            flush_clocked_inputs(reference);
            refresh_clocked_values(reference);
        }
    }

    variable_value get_clocked_variable(variable_type type,
        value_reference reference) override
    {
        const typed_value_reference variable{type, reference};
        auto value = clockController_->get_clocked_variable(type, reference);
        if (state_.clockedGetCache.contains(variable)) {
            state_.clockedGetCache.update_one(variable, std::move(value),
                duration::zero());
            return state_.clockedGetCache.get(variable);
        }
        return value;
    }

    variable_value probe_clocked_variable(variable_type type,
        value_reference reference) override
    {
        return clockController_->get_clocked_variable(type, reference);
    }

    void set_clocked_variable(variable_type type, value_reference reference,
        const variable_value& value) override
    {
        const typed_value_reference variable{type, reference};
        if (state_.clockedSetCache.contains(variable)) {
            state_.clockedSetCache.set(variable, value);
            clockController_->set_clocked_variable(
                type, reference,
                state_.clockedSetCache.modify_one(variable, duration::zero()));
            return;
        }
        clockController_->set_clocked_variable(type, reference, value);
    }

    clock_update_result update_clock_state() override
    {
        const auto result = clockController_->update_clock_state();
        return result;
    }

    event_update_result update_event_state() override
    {
        const auto result = (eventController_ != nullptr)
            ? eventController_->update_event_state()
            : clockController_->update_event_state();
        restrictedEventSets_.clear();
        return result;
    }

    void enter_step_mode() override
    {
        if (eventController_ != nullptr) {
            eventController_->enter_step_mode();
        } else {
            clockController_->enter_step_mode();
        }
    }

    void expose_for_getting(variable_type type, value_reference ref)
    {
        const auto& description = find_variable_description(ref, type);
        if (!description.clocks.empty()) {
            state_.clockedGetCache.expose({type, ref}, description);
            return;
        }
        if (!uses_legacy_cache(description)) {
            state_.wholeGetCache.expose({type, ref}, description);
            return;
        }
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

    [[nodiscard]] double get_real(value_reference ref) const
    {
        const auto& description =
            find_variable_description(ref, variable_type::real);
        if (!description.clocks.empty()) {
            return std::get<real_values>(
                state_.clockedGetCache.get({variable_type::real, ref})
                    .values())
                .values.at(0);
        }
        return state_.realGetCache.get(ref);
    }

    [[nodiscard]] int get_integer(value_reference ref) const
    {
        const auto& description =
            find_variable_description(ref, variable_type::integer);
        if (!description.clocks.empty()) {
            return std::get<integer_values>(
                state_.clockedGetCache.get({variable_type::integer, ref})
                    .values())
                .values.at(0);
        }
        return state_.integerGetCache.get(ref);
    }

    [[nodiscard]] bool get_boolean(value_reference ref) const
    {
        const auto& description =
            find_variable_description(ref, variable_type::boolean);
        if (!description.clocks.empty()) {
            return std::get<boolean_values>(
                state_.clockedGetCache.get({variable_type::boolean, ref})
                    .values())
                .values.at(0);
        }
        return state_.booleanGetCache.get(ref);
    }

    [[nodiscard]] std::string_view get_string(value_reference ref) const
    {
        const auto& description =
            find_variable_description(ref, variable_type::string);
        if (!description.clocks.empty()) {
            return std::get<string_values>(
                state_.clockedGetCache.get({variable_type::string, ref})
                    .values())
                .values.at(0);
        }
        return state_.stringGetCache.get(ref);
    }

    [[nodiscard]] variable_value get_variable_value(variable_type type,
        value_reference ref) const
    {
        const auto& description = find_variable_description(ref, type);
        if (!description.clocks.empty()) {
            return state_.clockedGetCache.get({type, ref});
        }
        if (uses_legacy_cache(description)) {
            switch (type) {
                case variable_type::real:
                    return variable_value(real_values{{get_real(ref)}});
                case variable_type::integer:
                    return variable_value(integer_values{{get_integer(ref)}});
                case variable_type::boolean:
                    return variable_value(boolean_values{{get_boolean(ref)}});
                case variable_type::string:
                    return variable_value(string_values{{std::string(get_string(ref))}});
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
                    COSIM_PANIC();
            }
            COSIM_PANIC();
        }
        return state_.wholeGetCache.get({type, ref});
    }

    void expose_for_setting(variable_type type, value_reference ref)
    {
        const auto& description = find_variable_description(ref, type);
        if (!description.clocks.empty()) {
            state_.clockedSetCache.expose({type, ref}, description);
            return;
        }
        if (!uses_legacy_cache(description)) {
            state_.wholeSetCache.expose({type, ref}, description);
            return;
        }
        switch (type) {
            case variable_type::real:
                state_.realSetCache.expose(ref, get_start_value<double>(description));
                return;
            case variable_type::integer:
                state_.integerSetCache.expose(ref, get_start_value<int>(description));
                return;
            case variable_type::boolean:
                state_.booleanSetCache.expose(ref, get_start_value<bool>(description));
                return;
            case variable_type::string:
                state_.stringSetCache.expose(ref,
                    get_start_value<std::string>(description));
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
                COSIM_PANIC();
        }
        COSIM_PANIC();
    }

    void set_real(value_reference ref, double value)
    {
        const auto& description =
            find_variable_description(ref, variable_type::real);
        if (!description.clocks.empty()) {
            state_.clockedSetCache.set({variable_type::real, ref},
                variable_value(real_values{{value}}));
            return;
        }
        if (state_.realSetCache.has_modifier(ref) &&
            state_.realSetCache.last_value_equals(ref, value)) {
            return;
        }
        state_.realSetCache.set_value(ref, value);
    }

    void set_integer(value_reference ref, int value)
    {
        const auto& description =
            find_variable_description(ref, variable_type::integer);
        if (!description.clocks.empty()) {
            state_.clockedSetCache.set({variable_type::integer, ref},
                variable_value(integer_values{{value}}));
            return;
        }
        if (state_.integerSetCache.has_modifier(ref) &&
            state_.integerSetCache.last_value_equals(ref, value)) {
            return;
        }
        state_.integerSetCache.set_value(ref, value);
    }

    void set_boolean(value_reference ref, bool value)
    {
        const auto& description =
            find_variable_description(ref, variable_type::boolean);
        if (!description.clocks.empty()) {
            state_.clockedSetCache.set({variable_type::boolean, ref},
                variable_value(boolean_values{{value}}));
            return;
        }
        if (state_.booleanSetCache.has_modifier(ref) &&
            state_.booleanSetCache.last_value_equals(ref, value)) {
            return;
        }
        state_.booleanSetCache.set_value(ref, value);
    }

    void set_string(value_reference ref, std::string_view value)
    {
        const auto& description =
            find_variable_description(ref, variable_type::string);
        if (!description.clocks.empty()) {
            state_.clockedSetCache.set(
                {variable_type::string, ref},
                variable_value(string_values{{std::string(value)}}));
            return;
        }
        if (state_.stringSetCache.has_modifier(ref) &&
            state_.stringSetCache.last_value_equals(ref, value)) {
            return;
        }
        state_.stringSetCache.set_value(ref, value);
    }

    void stage_clocked_input(variable_type type, value_reference ref,
        const variable_value& value)
    {
        if (find_variable_description(ref, type).clocks.empty()) {
            throw error(
                make_error_code(errc::invalid_system_structure),
                "Only Clock-associated inputs can be staged for Clock activation");
        }
        set_variable_value(type, ref, value);
    }

    void set_variable_value(variable_type type, value_reference ref,
        const variable_value& value)
    {
        const auto& description = find_variable_description(ref, type);
        const auto normalized = normalize_enumeration_value(type, value);
        if (normalized) {
            set_variable_value(type, ref, *normalized);
            return;
        }
        if (!description.clocks.empty()) {
            state_.clockedSetCache.set({type, ref}, value);
            return;
        }
        if (uses_legacy_cache(description)) {
            // NOLINTNEXTLINE(readability-simplify-boolean-expr): Preserve input-check diagnostics.
            COSIM_INPUT_CHECK(value.type() == type && value.is_scalar());
            switch (type) {
                case variable_type::real:
                    set_real(ref, std::get<real_values>(value.values()).values.at(0));
                    return;
                case variable_type::integer:
                    set_integer(ref, std::get<integer_values>(value.values()).values.at(0));
                    return;
                case variable_type::boolean:
                    set_boolean(ref, std::get<boolean_values>(value.values()).values.at(0));
                    return;
                case variable_type::string:
                    set_string(ref, std::get<string_values>(value.values()).values.at(0));
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
                    COSIM_PANIC();
            }
            COSIM_PANIC();
        }
        if (state_.wholeSetCache.has_modifier({type, ref}) &&
            state_.wholeSetCache.last_value_equals({type, ref}, value)) {
            return;
        }
        state_.wholeSetCache.set({type, ref}, value);
    }

    void
    set_real_input_modifier(value_reference ref,
        std::function<double(double, duration)> modifier)
    {
        if (is_clocked_scalar(ref, variable_type::real)) {
            set_variable_input_modifier(
                variable_type::real, ref,
                wrap_clocked_scalar_modifier<real_values>(std::move(modifier)));
            return;
        }
        state_.realSetCache.set_modifier(ref, modifier);
        replaySafeInputs_.erase({variable_type::real, ref});
        set_modified_reference(modifiedRealVariables_, ref, state_.realGetCache,
            state_.realSetCache);
    }

    void set_integer_input_modifier(value_reference ref,
        std::function<int(int, duration)> modifier)
    {
        if (is_clocked_scalar(ref, variable_type::integer)) {
            set_variable_input_modifier(
                variable_type::integer, ref,
                wrap_clocked_scalar_modifier<integer_values>(std::move(modifier)));
            return;
        }
        state_.integerSetCache.set_modifier(ref, modifier);
        replaySafeInputs_.erase({variable_type::integer, ref});
        set_modified_reference(modifiedIntegerVariables_, ref,
            state_.integerGetCache, state_.integerSetCache);
    }

    void
    set_boolean_input_modifier(value_reference ref,
        std::function<bool(bool, duration)> modifier)
    {
        if (is_clocked_scalar(ref, variable_type::boolean)) {
            set_variable_input_modifier(
                variable_type::boolean, ref,
                wrap_clocked_scalar_modifier<boolean_values>(std::move(modifier)));
            return;
        }
        state_.booleanSetCache.set_modifier(ref, modifier);
        replaySafeInputs_.erase({variable_type::boolean, ref});
        set_modified_reference(modifiedBooleanVariables_, ref,
            state_.booleanGetCache, state_.booleanSetCache);
    }

    void set_string_input_modifier(
        value_reference ref,
        std::function<std::string(std::string_view, duration)> modifier)
    {
        if (is_clocked_scalar(ref, variable_type::string)) {
            set_variable_input_modifier(
                variable_type::string, ref,
                wrap_clocked_scalar_modifier<string_values>(std::move(modifier)));
            return;
        }
        state_.stringSetCache.set_modifier(ref, modifier);
        replaySafeInputs_.erase({variable_type::string, ref});
        set_modified_reference(modifiedStringVariables_, ref, state_.stringGetCache,
            state_.stringSetCache);
    }

    void
    set_real_output_modifier(value_reference ref,
        std::function<double(double, duration)> modifier)
    {
        if (is_clocked_scalar(ref, variable_type::real)) {
            set_variable_output_modifier(
                variable_type::real, ref,
                wrap_clocked_scalar_modifier<real_values>(std::move(modifier)));
            return;
        }
        state_.realGetCache.set_modifier(ref, modifier);
        set_modified_reference(modifiedRealVariables_, ref, state_.realGetCache,
            state_.realSetCache);
    }

    void set_integer_output_modifier(value_reference ref,
        std::function<int(int, duration)> modifier)
    {
        if (is_clocked_scalar(ref, variable_type::integer)) {
            set_variable_output_modifier(
                variable_type::integer, ref,
                wrap_clocked_scalar_modifier<integer_values>(std::move(modifier)));
            return;
        }
        state_.integerGetCache.set_modifier(ref, modifier);
        set_modified_reference(modifiedIntegerVariables_, ref,
            state_.integerGetCache, state_.integerSetCache);
    }

    void
    set_boolean_output_modifier(value_reference ref,
        std::function<bool(bool, duration)> modifier)
    {
        if (is_clocked_scalar(ref, variable_type::boolean)) {
            set_variable_output_modifier(
                variable_type::boolean, ref,
                wrap_clocked_scalar_modifier<boolean_values>(std::move(modifier)));
            return;
        }
        state_.booleanGetCache.set_modifier(ref, modifier);
        set_modified_reference(modifiedBooleanVariables_, ref,
            state_.booleanGetCache, state_.booleanSetCache);
    }

    void set_string_output_modifier(
        value_reference ref,
        std::function<std::string(std::string_view, duration)> modifier)
    {
        if (is_clocked_scalar(ref, variable_type::string)) {
            set_variable_output_modifier(
                variable_type::string, ref,
                wrap_clocked_scalar_modifier<string_values>(std::move(modifier)));
            return;
        }
        state_.stringGetCache.set_modifier(ref, modifier);
        set_modified_reference(modifiedStringVariables_, ref, state_.stringGetCache,
            state_.stringSetCache);
    }

    void set_variable_input_modifier(variable_type type, value_reference ref,
        manipulable::variable_modifier modifier)
    {
        const auto& description = find_variable_description(ref, type);
        if (!description.clocks.empty()) {
            state_.clockedSetCache.set_modifier({type, ref}, std::move(modifier));
            replaySafeInputs_.erase({type, ref});
            if (description.dimensions.empty()) {
                update_clocked_modified_reference(type, ref);
            }
            return;
        }
        if (uses_legacy_cache(description)) {
            switch (type) {
                case variable_type::real:
                    set_real_input_modifier(
                        ref,
                        modifier
                            ? [modifier](double value, duration deltaT) {
                                  return apply_scalar_modifier<real_values>(
                                      modifier, value, deltaT);
                              }
                            : std::function<double(double, duration)>());
                    return;
                case variable_type::integer:
                    set_integer_input_modifier(
                        ref,
                        modifier
                            ? [modifier](int value, duration deltaT) {
                                  return apply_scalar_modifier<integer_values>(
                                      modifier, value, deltaT);
                              }
                            : std::function<int(int, duration)>());
                    return;
                case variable_type::boolean:
                    set_boolean_input_modifier(
                        ref,
                        modifier
                            ? [modifier](bool value, duration deltaT) {
                                  return apply_scalar_modifier<boolean_values>(
                                      modifier, value, deltaT);
                              }
                            : std::function<bool(bool, duration)>());
                    return;
                case variable_type::string:
                    set_string_input_modifier(
                        ref,
                        modifier
                            ? [modifier](std::string_view value, duration deltaT) {
                                  return apply_scalar_modifier<string_values>(
                                      modifier, std::string(value), deltaT);
                              }
                            : std::function<std::string(std::string_view, duration)>());
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
                    COSIM_PANIC();
            }
            COSIM_PANIC();
        }
        state_.wholeSetCache.set_modifier({type, ref}, std::move(modifier));
        replaySafeInputs_.erase({type, ref});
    }

    void set_variable_output_modifier(variable_type type, value_reference ref,
        manipulable::variable_modifier modifier)
    {
        const auto& description = find_variable_description(ref, type);
        if (!description.clocks.empty()) {
            state_.clockedGetCache.set_modifier({type, ref}, std::move(modifier));
            if (description.dimensions.empty()) {
                update_clocked_modified_reference(type, ref);
            }
            return;
        }
        if (uses_legacy_cache(description)) {
            switch (type) {
                case variable_type::real:
                    set_real_output_modifier(
                        ref,
                        modifier
                            ? [modifier](double value, duration deltaT) {
                                  return apply_scalar_modifier<real_values>(
                                      modifier, value, deltaT);
                              }
                            : std::function<double(double, duration)>());
                    return;
                case variable_type::integer:
                    set_integer_output_modifier(
                        ref,
                        modifier
                            ? [modifier](int value, duration deltaT) {
                                  return apply_scalar_modifier<integer_values>(
                                      modifier, value, deltaT);
                              }
                            : std::function<int(int, duration)>());
                    return;
                case variable_type::boolean:
                    set_boolean_output_modifier(
                        ref,
                        modifier
                            ? [modifier](bool value, duration deltaT) {
                                  return apply_scalar_modifier<boolean_values>(
                                      modifier, value, deltaT);
                              }
                            : std::function<bool(bool, duration)>());
                    return;
                case variable_type::string:
                    set_string_output_modifier(
                        ref,
                        modifier
                            ? [modifier](std::string_view value, duration deltaT) {
                                  return apply_scalar_modifier<string_values>(
                                      modifier, std::string(value), deltaT);
                              }
                            : std::function<std::string(std::string_view, duration)>());
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
                    COSIM_PANIC();
            }
            COSIM_PANIC();
        }
        state_.wholeGetCache.set_modifier({type, ref}, std::move(modifier));
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

    [[nodiscard]] std::vector<typed_value_reference>
    get_modified_variables() const
    {
        std::set<std::pair<variable_type, value_reference>> modified;
        const auto add = [&modified](variable_type type, const auto& references) {
            for (const auto reference : references) {
                modified.emplace(type, reference);
            }
        };
        add(variable_type::real, modifiedRealVariables_);
        add(variable_type::integer, modifiedIntegerVariables_);
        add(variable_type::boolean, modifiedBooleanVariables_);
        add(variable_type::string, modifiedStringVariables_);
        const auto addCache = [&modified](const auto& cache) {
            for (std::size_t i = 0; i < cache.references.size(); ++i) {
                if (cache.modifiers.at(i)) {
                    const auto& reference = cache.references.at(i);
                    modified.emplace(reference.type, reference.reference);
                }
            }
        };
        addCache(state_.wholeGetCache);
        addCache(state_.wholeSetCache);
        addCache(state_.clockedGetCache);
        addCache(state_.clockedSetCache);
        std::vector<typed_value_reference> result;
        result.reserve(modified.size());
        for (const auto& entry : modified) {
            result.push_back({entry.first, entry.second});
        }
        return result;
    }

    [[nodiscard]] bool has_variable_modifiers() const
    {
        return has_active_modifiers();
    }

    [[nodiscard]] bool supports_automatic_step_replay() const
    {
        return slave_->supports_automatic_step_replay() && !has_active_modifiers();
    }

    void setup(time_point startTime, std::optional<time_point> stopTime,
        std::optional<double> relativeTolerance)
    {
        const auto deltaT = duration::zero();
        const auto filter = [this](const variable_type type) {
            return [this, type](const value_reference reference,
                       const cosim::scalar_value&) {
                const auto& description =
                    this->find_variable_description(reference, type);
                return description.variability != variable_variability::constant &&
                    description.causality != variable_causality::input;
            };
        };

        const auto [realRefs, realValues] =
            state_.realSetCache.modify_and_get(deltaT, filter(variable_type::real));
        const auto [integerRefs, integerValues] =
            state_.integerSetCache.modify_and_get(deltaT,
                filter(variable_type::integer));
        const auto [booleanRefs, booleanValues] =
            state_.booleanSetCache.modify_and_get(deltaT,
                filter(variable_type::boolean));
        const auto [stringRefs, stringValues] =
            state_.stringSetCache.modify_and_get(deltaT,
                filter(variable_type::string));
        const auto [wholeRefs, wholeValues] = state_.wholeSetCache.modify_and_get(
            deltaT, [this](typed_value_reference variable, const variable_value&) {
                const auto& description = this->find_variable_description(
                    variable.reference, variable.type);
                return description.variability != variable_variability::constant &&
                    description.causality != variable_causality::input;
            });

        slave_->set_variables(
            gsl::make_span(realRefs), gsl::make_span(realValues),
            gsl::make_span(integerRefs), gsl::make_span(integerValues),
            gsl::make_span(booleanRefs), gsl::make_span(booleanValues),
            gsl::make_span(stringRefs), gsl::make_span(stringValues));
        slave_->set_variable_values(gsl::make_span(wholeRefs),
            gsl::make_span(wholeValues));

        slave_->setup(startTime, stopTime, relativeTolerance);
        const auto [clockedRefs, clockedValues] =
            state_.clockedSetCache.modify_and_get(deltaT);
        slave_->set_variable_values(gsl::make_span(clockedRefs),
            gsl::make_span(clockedValues));
        get_variables(duration::zero(), true);
    }

    void do_iteration()
    {
        set_variables(duration::zero(), true);
        get_variables(duration::zero(), true);
    }

    void start_simulation()
    {
        set_variables(duration::zero(), true);
        slave_->start_simulation();
        simulationStarted_ = true;
        if (!slave_->get_event_handling_status().termination_requested) {
            get_variables(duration::zero());
        }
    }

    bool apply_pending_step_inputs(duration deltaT)
    {
        check_step_size(deltaT);
        if (!supports_automatic_step_replay() || inputEventPrepared_ ||
            !inputDiscontinuities_.empty()) {
            return false;
        }
        apply_step_inputs(deltaT);
        return true;
    }

    step_result do_step(time_point currentT, duration deltaT)
    {
        check_step_size(deltaT);
        apply_step_inputs(deltaT);
        const auto result = slave_->do_step(currentT, deltaT);
        if (result == step_result::complete &&
            !modelDescription_.capabilities.can_handle_variable_step) {
            state_.fixedStepSize = deltaT;
        }
        get_variables(deltaT);
        return result;
    }

    step_result_info do_step_with_info(time_point currentT, duration deltaT)
    {
        check_step_size(deltaT);
        apply_step_inputs(deltaT);
        auto result = slave_->do_step_with_info(currentT, deltaT);
        if (result.early_return &&
            (result.last_successful_time < currentT ||
                result.last_successful_time > currentT + deltaT)) {
            throw error(
                make_error_code(errc::simulation_error),
                name_ +
                    ": simulator returned an endpoint outside the requested step");
        }
        if (result.result == step_result::complete &&
            !result.termination_requested) {
            if (!modelDescription_.capabilities.can_handle_variable_step) {
                state_.fixedStepSize = deltaT;
            }
            get_variables(result.early_return ? result.last_successful_time - currentT
                                              : deltaT);
        }
        return result;
    }

    void handle_events()
    {
        slave_->handle_events();
        if (!slave_->get_event_handling_status().termination_requested) {
            get_variables(duration::zero(), false, true);
        }
    }

    [[nodiscard]] event_handling_status
    get_event_handling_status() const noexcept
    {
        return slave_->get_event_handling_status();
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
    static constexpr std::int32_t export_scheme_version = 4;

    [[nodiscard]] serialization::node export_state(state_index stateIndex) const
    {
        serialization::node exportedState;
        exportedState.put("scheme_version", export_scheme_version);
        exportedState.put_child("state", slave_->export_state(stateIndex));
        const auto& savedState = savedStates_.at(stateIndex);
        exportedState.put("has_fixed_step_size",
            savedState.fixedStepSize.has_value());
        if (savedState.fixedStepSize) {
            exportedState.put<duration::rep>("fixed_step_size_ticks",
                savedState.fixedStepSize->count());
        }
        exportedState.put_child("real_get_cache",
            savedState.realGetCache.export_state());
        exportedState.put_child("integer_get_cache",
            savedState.integerGetCache.export_state());
        exportedState.put_child("boolean_get_cache",
            savedState.booleanGetCache.export_state());
        exportedState.put_child("string_get_cache",
            savedState.stringGetCache.export_state());
        exportedState.put_child("real_set_cache",
            savedState.realSetCache.export_state_v1());
        exportedState.put_child("integer_set_cache",
            savedState.integerSetCache.export_state_v1());
        exportedState.put_child("boolean_set_cache",
            savedState.booleanSetCache.export_state_v1());
        exportedState.put_child("string_set_cache",
            savedState.stringSetCache.export_state_v1());
        exportedState.put_child("whole_get_cache",
            savedState.wholeGetCache.export_state());
        exportedState.put_child("whole_set_cache",
            savedState.wholeSetCache.export_state());
        exportedState.put_child("clocked_get_cache",
            savedState.clockedGetCache.export_state());
        exportedState.put_child("clocked_set_cache",
            savedState.clockedSetCache.export_state());
        exportedState.put("has_deferred_time", savedState.deferredAt.has_value());
        if (savedState.deferredAt) {
            exportedState.put<std::int64_t>(
                "deferred_time_ticks",
                savedState.deferredAt->time_since_epoch().count());
        }
        serialization::node deferredInputs;
        for (std::size_t i = 0; i < savedState.deferredInputs.size(); ++i) {
            const auto& [reference, value] = savedState.deferredInputs.at(i);
            serialization::node entry;
            entry.put("type", static_cast<int>(reference.type));
            entry.put("reference", reference.reference);
            entry.put_child("value", export_variable_value(value));
            deferredInputs.put_child(std::to_string(i), entry);
        }
        exportedState.put_child("deferred_inputs", deferredInputs);
        serialization::node discontinuities;
        std::size_t index = 0;
        for (const auto& [type, reference] : savedState.deferredDiscontinuities) {
            serialization::node entry;
            entry.put("type", static_cast<int>(type));
            entry.put("reference", reference);
            discontinuities.put_child(std::to_string(index++), entry);
        }
        exportedState.put_child("deferred_discontinuities", discontinuities);
        serialization::node policies;
        index = 0;
        for (const auto& [type, reference] : savedState.sampleDiscontinuityInputs) {
            serialization::node entry;
            entry.put("type", static_cast<int>(type));
            entry.put("reference", reference);
            policies.put_child(std::to_string(index++), entry);
        }
        exportedState.put_child("sample_discontinuity_inputs", policies);
        return exportedState;
    }

private:
    void import_fixed_step_state(
        const serialization::node& exportedState, std::int32_t schemeVersion, state& importedState) const
    {
        if (schemeVersion >= 4) {
            if (exportedState.get<bool>("has_fixed_step_size")) {
                const auto ticks =
                    exportedState.get<duration::rep>("fixed_step_size_ticks");
                if (ticks <= 0 ||
                    modelDescription_.capabilities.can_handle_variable_step) {
                    throw std::invalid_argument(
                        "Invalid fixed communication step size");
                }
                importedState.fixedStepSize = duration(ticks);
            } else if (exportedState.get_optional<duration::rep>(
                           "fixed_step_size_ticks")) {
                throw std::invalid_argument(
                    "Unexpected fixed communication step size");
            }
        } else if (!modelDescription_.capabilities.can_handle_variable_step) {
            throw error(make_error_code(errc::unsupported_feature),
                name_ + ": the snapshot does not contain the fixed "
                        "communication step size");
        }
    }

    [[nodiscard]] std::pair<typed_value_reference, variable_description>
    legacy_enumeration_variable(const std::string& key) const
    {
        value_reference reference{};
        const auto* const end = key.data() + key.size(); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): from_chars requires a pointer range.
        const auto parsed =
            std::from_chars(key.data(), end, reference);
        if (parsed.ec != std::errc{} || parsed.ptr != end) {
            throw std::invalid_argument("Invalid legacy Enumeration reference");
        }
        auto description =
            find_variable_description(reference, variable_type::enumeration);
        if (!description.dimensions.empty() || !description.clocks.empty()) {
            throw std::invalid_argument("Invalid legacy Enumeration shape");
        }
        return std::make_pair(
            typed_value_reference{variable_type::enumeration, reference},
            std::move(description));
    }

    [[nodiscard]] static std::optional<variable_value> legacy_enumeration_value(
        const serialization::node& entry)
    {
        if (!entry.get<bool>("has_value")) {
            if (entry.get_child_optional("value")) {
                throw std::invalid_argument(
                    "Unexpected legacy Enumeration value");
            }
            return std::nullopt;
        }
        return variable_value(enumeration_values{
            {enumeration_value{entry.get<std::int64_t>("value")}},
        });
    }

    void import_legacy_enumeration_get_cache(
        const serialization::node& cache, state& importedState) const
    {
        for (const auto& [key, entry] : cache) {
            const auto [variable, description] = legacy_enumeration_variable(key);
            if (importedState.wholeGetCache.contains(variable)) {
                throw std::invalid_argument(
                    "Duplicate legacy Enumeration reference");
            }
            importedState.wholeGetCache.expose(variable, description);
            importedState.wholeGetCache.values.back() = legacy_enumeration_value(entry);
        }
    }

    void import_legacy_enumeration_set_cache(
        const serialization::node& cache, state& importedState) const
    {
        for (const auto& [key, entry] : cache) {
            const auto [variable, description] = legacy_enumeration_variable(key);
            if (importedState.wholeSetCache.contains(variable)) {
                throw std::invalid_argument(
                    "Duplicate legacy Enumeration reference");
            }
            auto value = legacy_enumeration_value(entry);
            const auto pending = entry.get<bool>("pending");
            if (pending && !value) {
                throw std::invalid_argument(
                    "Pending legacy Enumeration has no value");
            }
            importedState.wholeSetCache.expose(variable, description);
            importedState.wholeSetCache.values.back() = std::move(value);
            importedState.wholeSetCache.pending.back() = pending;
        }
    }

    void restore_legacy_enumeration_exposures(state& importedState) const
    {
        for (const auto variable : state_.wholeGetCache.references) {
            if (variable.type != variable_type::enumeration) {
                continue;
            }
            const auto& description =
                find_variable_description(variable.reference, variable.type);
            if (description.dimensions.empty()) {
                importedState.wholeGetCache.expose(variable, description);
            }
        }
        for (const auto variable : state_.wholeSetCache.references) {
            if (variable.type != variable_type::enumeration) {
                continue;
            }
            const auto& description =
                find_variable_description(variable.reference, variable.type);
            if (description.dimensions.empty()) {
                importedState.wholeSetCache.expose(variable, description);
                importedState.wholeSetCache.values.back() = std::nullopt;
            }
        }
    }

    void import_legacy_enumeration_caches(
        const serialization::node& exportedState, state& importedState) const
    {
        const auto getNode =
            exportedState.get_child_optional("enumeration_get_cache");
        const auto setNode =
            exportedState.get_child_optional("enumeration_set_cache");
        if (static_cast<bool>(getNode) != static_cast<bool>(setNode) ||
            exportedState.count("enumeration_get_cache") > 1 ||
            exportedState.count("enumeration_set_cache") > 1) {
            throw std::invalid_argument("Invalid legacy Enumeration caches");
        }
        if (getNode) {
            if (!std::holds_alternative<std::nullptr_t>(getNode->data()) ||
                !std::holds_alternative<std::nullptr_t>(setNode->data())) {
                throw std::invalid_argument("Invalid legacy Enumeration cache");
            }
            import_legacy_enumeration_get_cache(*getNode, importedState);
            import_legacy_enumeration_set_cache(*setNode, importedState);
        } else {
            restore_legacy_enumeration_exposures(importedState);
        }
    }

    [[nodiscard]] std::pair<typed_value_reference, variable_description> parse_deferred_reference(
        const serialization::node& entry, const state& importedState) const
    {
        const auto rawType = entry.get<int>("type");
        if (rawType < static_cast<int>(variable_type::real) ||
            rawType >= static_cast<int>(variable_type::clock)) {
            throw std::invalid_argument("Invalid deferred input type");
        }
        const typed_value_reference reference{
            static_cast<variable_type>(rawType),
            entry.get<value_reference>("reference"),
        };
        const auto& description =
            find_variable_description(reference.reference, reference.type);
        const bool exposed = [&] {
            if (!uses_legacy_cache(description)) {
                return importedState.wholeSetCache.contains(reference);
            }
            switch (reference.type) {
                case variable_type::real:
                    return importedState.realSetCache.contains(reference.reference);
                case variable_type::integer:
                    return importedState.integerSetCache.contains(reference.reference);
                case variable_type::boolean:
                    return importedState.booleanSetCache.contains(reference.reference);
                default:
                    return importedState.stringSetCache.contains(reference.reference);
            }
        }();
        if (!exposed || !description.clocks.empty()) {
            throw std::invalid_argument("Invalid deferred input reference");
        }
        return std::make_pair(reference, description);
    }

    void import_discontinuity_references(const serialization::node& entries,
        state& importedState,
        std::set<std::pair<variable_type, value_reference>>& destination,
        const char* invalidMessage) const
    {
        for (std::size_t i = 0; i < entries.size(); ++i) {
            const auto [reference, description] =
                parse_deferred_reference(entries.get_child(std::to_string(i)), importedState);
            if (description.causality != variable_causality::input ||
                description.variability != variable_variability::continuous ||
                (reference.type != variable_type::real &&
                    reference.type != variable_type::float32) ||
                !destination.emplace(reference.type, reference.reference).second) {
                throw std::invalid_argument(invalidMessage);
            }
        }
    }

    void import_deferred_values(
        const serialization::node& deferredInputs, state& importedState) const
    {
        for (std::size_t i = 0; i < deferredInputs.size(); ++i) {
            const auto& entry = deferredInputs.get_child(std::to_string(i));
            const auto [reference, description] = parse_deferred_reference(entry, importedState);
            auto value = import_variable_value(entry.get_child("value"));
            const bool eventInput =
                (description.causality == variable_causality::input &&
                    description.variability == variable_variability::discrete) ||
                (description.causality == variable_causality::parameter &&
                    description.variability == variable_variability::tunable) ||
                importedState.deferredDiscontinuities.count(
                    {reference.type, reference.reference}) != 0;
            const auto duplicate = boost::range::find_if(
                importedState.deferredInputs,
                [reference = reference](const auto& candidate) {
                    return candidate.first == reference;
                });
            if (!eventInput || value.type() != reference.type ||
                duplicate != importedState.deferredInputs.end()) {
                throw std::invalid_argument("Invalid deferred event input");
            }
            if (uses_legacy_cache(description)) {
                if (!value.is_scalar()) {
                    throw std::invalid_argument("Invalid deferred scalar shape");
                }
            } else {
                const auto index = importedState.wholeSetCache.indices.at(
                    cache_key(reference.type, reference.reference));
                const auto& cached = importedState.wholeSetCache.values.at(index);
                if (!cached || cached->dimensions() != value.dimensions()) {
                    throw std::invalid_argument("Invalid deferred array shape");
                }
            }
            importedState.deferredInputs.emplace_back(reference, std::move(value));
        }
    }

    void import_deferred_state(
        const serialization::node& exportedState, state& importedState) const
    {
        if (exportedState.get<bool>("has_deferred_time")) {
            importedState.deferredAt = time_point(duration(
                exportedState.get<duration::rep>("deferred_time_ticks")));
        } else if (exportedState.get_optional<duration::rep>(
                       "deferred_time_ticks")) {
            throw std::invalid_argument("Unexpected deferred event time");
        }
        if (const auto policies = exportedState.get_child_optional(
                "sample_discontinuity_inputs")) {
            import_discontinuity_references(*policies, importedState,
                importedState.sampleDiscontinuityInputs, "Invalid sample discontinuity policy");
        }
        const auto& discontinuities = exportedState.get_child("deferred_discontinuities");
        import_discontinuity_references(discontinuities, importedState,
            importedState.deferredDiscontinuities, "Invalid deferred discontinuity");
        const auto& deferredInputs = exportedState.get_child("deferred_inputs");
        import_deferred_values(deferredInputs, importedState);
        if ((!importedState.deferredInputs.empty() ||
                !importedState.deferredDiscontinuities.empty()) &&
            (!importedState.deferredAt || (eventController_ == nullptr))) {
            throw std::invalid_argument(
                "Deferred events require an Event Mode controller and time");
        }
    }

public:
    state_index import_state(const serialization::node& exportedState)
    {
        try {
            const auto schemeVersion =
                exportedState.get<std::int32_t>("scheme_version");
            if (schemeVersion < 0 || schemeVersion > export_scheme_version) {
                throw error(make_error_code(errc::bad_file),
                    "The serialized state of subsimulator '" + name_ +
                        "' uses an incompatible scheme");
            }
            state importedState;
            import_fixed_step_state(exportedState, schemeVersion, importedState);
            importedState.realGetCache.import_state(
                exportedState.get_child("real_get_cache"));
            importedState.integerGetCache.import_state(
                exportedState.get_child("integer_get_cache"));
            importedState.booleanGetCache.import_state(
                exportedState.get_child("boolean_get_cache"));
            importedState.stringGetCache.import_state(
                exportedState.get_child("string_get_cache"));
            if (schemeVersion >= 1) {
                importedState.realSetCache.import_state_v1(
                    exportedState.get_child("real_set_cache"));
                importedState.integerSetCache.import_state_v1(
                    exportedState.get_child("integer_set_cache"));
                importedState.booleanSetCache.import_state_v1(
                    exportedState.get_child("boolean_set_cache"));
                importedState.stringSetCache.import_state_v1(
                    exportedState.get_child("string_set_cache"));
                importedState.wholeGetCache.import_state(
                    exportedState.get_child("whole_get_cache"));
                importedState.wholeSetCache.import_state(
                    exportedState.get_child("whole_set_cache"));
                if (schemeVersion >= 2) {
                    importedState.clockedGetCache.import_state(
                        exportedState.get_child("clocked_get_cache"));
                    importedState.clockedSetCache.import_state(
                        exportedState.get_child("clocked_set_cache"));
                }
            } else {
                importedState.realSetCache.import_state(
                    exportedState.get_child("real_set_cache"));
                importedState.integerSetCache.import_state(
                    exportedState.get_child("integer_set_cache"));
                importedState.booleanSetCache.import_state(
                    exportedState.get_child("boolean_set_cache"));
                importedState.stringSetCache.import_state(
                    exportedState.get_child("string_set_cache"));

                import_legacy_enumeration_caches(exportedState, importedState);
            }
            if (schemeVersion >= 3) {
                import_deferred_state(exportedState, importedState);
            }
            if (importedState.sampleDiscontinuityInputs !=
                state_.sampleDiscontinuityInputs) {
                throw std::invalid_argument("Incompatible sample discontinuity policy");
            }
            const auto stateIndex =
                slave_->import_state(exportedState.get_child("state"));
            assert(savedStates_.count(stateIndex) == 0);
            try {
                savedStates_.emplace(stateIndex, std::move(importedState));
            } catch (...) {
                slave_->release_state(stateIndex);
                throw;
            }
            return stateIndex;
        } catch (const boost::property_tree::ptree_bad_path&) {
            throw error(make_error_code(errc::bad_file),
                "The serialized state of subsimulator '" + name_ +
                    "' is invalid or corrupt");
        } catch (const boost::property_tree::ptree_bad_data&) {
            throw error(make_error_code(errc::bad_file),
                "The serialized state of subsimulator '" + name_ +
                    "' is invalid or corrupt");
        } catch (const std::bad_variant_access&) {
            throw error(make_error_code(errc::bad_file),
                "The serialized state of subsimulator '" + name_ +
                    "' is invalid or corrupt");
        } catch (const std::invalid_argument&) {
            throw error(make_error_code(errc::bad_file),
                "The serialized state of subsimulator '" + name_ +
                    "' is invalid or corrupt");
        } catch (const std::out_of_range&) {
            throw error(make_error_code(errc::bad_file),
                "The serialized state of subsimulator '" + name_ +
                    "' is invalid or corrupt");
        }
    }

private:
    void check_step_size(duration deltaT) const
    {
        if (state_.fixedStepSize && *state_.fixedStepSize != deltaT) {
            throw error(
                make_error_code(errc::unsupported_feature),
                name_ +
                    ": the FMU does not support variable communication step sizes");
        }
    }

    [[nodiscard]] bool is_clocked_scalar(value_reference ref,
        variable_type type) const
    {
        const auto& description = find_variable_description(ref, type);
        if (description.clocks.empty()) {
            return false;
        }
        if (!description.dimensions.empty()) {
            throw error(make_error_code(errc::unsupported_feature),
                "Scalar modifiers cannot be applied to an array variable");
        }
        return true;
    }

    void update_clocked_modified_reference(variable_type type,
        value_reference ref)
    {
        std::unordered_set<value_reference>* modified = nullptr;
        switch (type) {
            case variable_type::real:
                modified = &modifiedRealVariables_;
                break;
            case variable_type::integer:
                modified = &modifiedIntegerVariables_;
                break;
            case variable_type::boolean:
                modified = &modifiedBooleanVariables_;
                break;
            case variable_type::string:
                modified = &modifiedStringVariables_;
                break;
            default:
                return;
        }
        const typed_value_reference variable{type, ref};
        if (state_.clockedGetCache.has_modifier(variable) ||
            state_.clockedSetCache.has_modifier(variable)) {
            modified->insert(ref);
        } else {
            modified->erase(ref);
        }
    }

    void flush_clocked_inputs(value_reference clock)
    {
        std::set<std::pair<variable_type, value_reference>> processed;
        for (const auto& variable : modelDescription_.variables) {
            if (boost::range::find(variable.clocks, clock) == variable.clocks.end()) {
                continue;
            }
            const auto key = std::make_pair(variable.type, variable.reference);
            if (!processed.insert(key).second) {
                continue;
            }
            const typed_value_reference reference{variable.type, variable.reference};
            if (!state_.clockedSetCache.contains(reference) ||
                !state_.clockedSetCache.needs_application(reference)) {
                continue;
            }
            clockController_->set_clocked_variable(
                variable.type, variable.reference,
                state_.clockedSetCache.modify_one(reference, duration::zero()));
        }
    }

    void refresh_clocked_values(value_reference clock)
    {
        for (const auto& variable : modelDescription_.variables) {
            if (boost::range::find(variable.clocks, clock) == variable.clocks.end()) {
                continue;
            }
            if (state_.clockedGetCache.contains(
                    {variable.type, variable.reference})) {
                (void)get_clocked_variable(variable.type, variable.reference);
            }
        }
    }

    void apply_prepared_inputs()
    {
        std::vector<typed_value_reference> references;
        std::vector<variable_value> values;
        references.reserve(preparedInputs_.size());
        values.reserve(preparedInputs_.size());
        for (const auto& entry : preparedInputs_) {
            references.push_back(entry.first);
            values.push_back(entry.second);
        }
        slave_->set_variable_values(gsl::make_span(references),
            gsl::make_span(values));
        preparedInputs_.clear();
    }

    void apply_step_inputs(duration deltaT)
    {
        if (inputEventPrepared_ || !inputDiscontinuities_.empty()) {
            throw error(
                make_error_code(errc::unsupported_feature),
                name_ +
                    ": the pending input event must be coordinated before stepping");
        }
        if (inputsPrepared_) {
            apply_prepared_inputs();
            inputsPrepared_ = false;
        } else {
            set_variables(deltaT);
        }
    }

    void set_variables(duration deltaT, bool includeClocked = false)
    {
        const auto [realRefs, realValues] =
            state_.realSetCache.modify_and_get(deltaT);
        const auto [integerRefs, integerValues] =
            state_.integerSetCache.modify_and_get(deltaT);
        const auto [booleanRefs, booleanValues] =
            state_.booleanSetCache.modify_and_get(deltaT);
        const auto [stringRefs, stringValues] =
            state_.stringSetCache.modify_and_get(deltaT);
        const auto [wholeRefs, wholeValues] =
            state_.wholeSetCache.modify_and_get(deltaT);
        slave_->set_variables(
            gsl::make_span(realRefs), gsl::make_span(realValues),
            gsl::make_span(integerRefs), gsl::make_span(integerValues),
            gsl::make_span(booleanRefs), gsl::make_span(booleanValues),
            gsl::make_span(stringRefs), gsl::make_span(stringValues));
        slave_->set_variable_values(gsl::make_span(wholeRefs),
            gsl::make_span(wholeValues));
        if (includeClocked) {
            const auto [clockedRefs, clockedValues] =
                state_.clockedSetCache.modify_and_get(deltaT);
            slave_->set_variable_values(gsl::make_span(clockedRefs),
                gsl::make_span(clockedValues));
        }
        state_.realSetCache.reset();
        state_.integerSetCache.reset();
        state_.booleanSetCache.reset();
        state_.stringSetCache.reset();
    }

    void get_variables(duration deltaT, bool includeClocked = false,
        bool reuseUnchangedModifiers = false)
    {
        slave_->get_variables(&variableValues_,
            gsl::make_span(state_.realGetCache.references),
            gsl::make_span(state_.integerGetCache.references),
            gsl::make_span(state_.booleanGetCache.references),
            gsl::make_span(state_.stringGetCache.references));
        copy_contents(variableValues_.real, state_.realGetCache.originalValues);
        copy_contents(variableValues_.integer,
            state_.integerGetCache.originalValues);
        copy_contents(variableValues_.boolean,
            state_.booleanGetCache.originalValues);
        copy_contents(variableValues_.string, state_.stringGetCache.originalValues);
        state_.wholeGetCache.update(slave_->get_variable_values(gsl::make_span(
                                        state_.wholeGetCache.references)),
            deltaT, reuseUnchangedModifiers);
        if (includeClocked) {
            state_.clockedGetCache.update(slave_->get_variable_values(gsl::make_span(
                                              state_.clockedGetCache.references)),
                deltaT);
        }
        state_.realGetCache.run_modifiers(deltaT, reuseUnchangedModifiers);
        state_.integerGetCache.run_modifiers(deltaT, reuseUnchangedModifiers);
        state_.booleanGetCache.run_modifiers(deltaT, reuseUnchangedModifiers);
        state_.stringGetCache.run_modifiers(deltaT, reuseUnchangedModifiers);
    }

    [[nodiscard]] const variable_description&
    find_variable_description(value_reference ref, variable_type type) const
    {
        auto const found = boost::range::find_if(
            modelDescription_.variables, [type, ref](const auto& variable) {
                return variable.type == type && variable.reference == ref;
            });
        if (found == modelDescription_.variables.end()) {
            std::ostringstream oss;
            oss << "Variable with value reference " << ref << " and type " << type
                << " not found in model description for " << name_;
            throw std::out_of_range(oss.str());
        }
        return *found;
    }

    template<typename T>
    void set_modified_reference(std::unordered_set<value_reference>& modifiedRefs,
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

    [[nodiscard]] bool has_active_modifiers() const
    {
        return !modifiedRealVariables_.empty() ||
            !modifiedIntegerVariables_.empty() ||
            !modifiedBooleanVariables_.empty() ||
            !modifiedStringVariables_.empty() ||
            state_.wholeGetCache.has_modifiers() ||
            state_.wholeSetCache.has_modifiers() ||
            state_.clockedGetCache.has_modifiers() ||
            state_.clockedSetCache.has_modifiers();
    }

    void check_state_saving_allowed() const
    {
        if (inputsPrepared_ || !inputDiscontinuities_.empty()) {
            throw error(make_error_code(errc::unsupported_feature),
                "Cannot save or restore subsimulator state while input "
                "events are pending");
        }
        if (!has_active_modifiers()) {
            return;
        }
        throw error(make_error_code(errc::unsupported_feature),
            "Cannot save or restore subsimulator state when variable "
            "modifiers are active");
    }

    std::shared_ptr<slave> slave_;
    std::string name_;
    cosim::model_description modelDescription_;
    clock_controller* clockController_;
    event_controller* eventController_;
    bool simulationStarted_ = false;
    bool inputsPrepared_ = false;
    bool inputEventPrepared_ = false;
    std::set<std::pair<variable_type, value_reference>> changedEventInputs_;
    std::set<std::pair<variable_type, value_reference>> restrictedEventSets_;
    std::vector<std::pair<typed_value_reference, variable_value>> preparedInputs_;
    std::set<std::pair<variable_type, value_reference>> inputDiscontinuities_;
    std::set<std::pair<variable_type, value_reference>> replaySafeInputs_;

    struct state // NOLINT(bugprone-exception-escape): MSVC unordered-container moves may allocate.
    {
        get_variable_cache<double> realGetCache;
        get_variable_cache<int> integerGetCache;
        get_variable_cache<bool> booleanGetCache;
        get_variable_cache<std::string> stringGetCache;

        set_variable_cache<double> realSetCache;
        set_variable_cache<int> integerSetCache;
        set_variable_cache<bool> booleanSetCache;
        set_variable_cache<std::string> stringSetCache;
        whole_get_cache wholeGetCache;
        whole_set_cache wholeSetCache;
        whole_get_cache clockedGetCache;
        whole_set_cache clockedSetCache;
        std::vector<std::pair<typed_value_reference, variable_value>>
            deferredInputs;
        std::set<std::pair<variable_type, value_reference>> deferredDiscontinuities;
        std::optional<time_point> deferredAt;
        std::set<std::pair<variable_type, value_reference>>
            sampleDiscontinuityInputs;
        std::optional<duration> fixedStepSize;
    };
    state state_;
    std::unordered_map<state_index, state> savedStates_;

    std::unordered_set<value_reference> modifiedRealVariables_;
    std::unordered_set<value_reference> modifiedIntegerVariables_;
    std::unordered_set<value_reference> modifiedBooleanVariables_;
    std::unordered_set<value_reference> modifiedStringVariables_;

    // This is a temporary storage which gets reused within certain functions
    // to avoid frequent reallocations.
    cosim::slave::variable_values variableValues_;
};

slave_simulator::slave_simulator(std::shared_ptr<slave> slave,
    std::string_view name)
    : pimpl_(std::make_unique<impl>(std::move(slave), name))
{ }

slave_simulator::~slave_simulator() noexcept = default;

slave_simulator::slave_simulator(slave_simulator&&) noexcept = default;

slave_simulator&
slave_simulator::operator=(slave_simulator&&) noexcept = default;

std::shared_ptr<slave> slave_simulator::get_slave() const
{
    return pimpl_->get_slave();
}

std::string slave_simulator::name() const { return pimpl_->name(); }

cosim::model_description slave_simulator::model_description() const
{
    return pimpl_->model_description();
}

clock_controller* slave_simulator::get_clock_controller() noexcept
{
    return pimpl_->get_clock_controller();
}

event_controller* slave_simulator::get_event_controller() noexcept
{
    return pimpl_->get_event_controller();
}

void slave_simulator::begin_input_event_coordination(time_point currentTime)
{
    pimpl_->begin_input_event_coordination(currentTime);
}

void slave_simulator::defer_input_events(time_point currentTime)
{
    pimpl_->defer_input_events(currentTime);
}

bool slave_simulator::prepare_input_events(duration deltaT,
    bool evaluateUnstagedModifiers)
{
    return pimpl_->prepare_input_events(deltaT, evaluateUnstagedModifiers);
}

bool slave_simulator::reevaluate_input_modifiers(duration deltaT)
{
    return pimpl_->reevaluate_input_modifiers(deltaT);
}

void slave_simulator::apply_event_inputs() { pimpl_->apply_event_inputs(); }

void slave_simulator::apply_coupled_event_inputs()
{
    pimpl_->apply_event_inputs(true);
}

bool slave_simulator::has_applicable_event_inputs() const
{
    return pimpl_->has_applicable_event_inputs();
}

void slave_simulator::finish_input_event_coordination()
{
    pimpl_->finish_input_event_coordination();
}

void slave_simulator::refresh_event_values() { pimpl_->refresh_event_values(); }

variable_value
slave_simulator::get_event_output_value(variable_type type,
    value_reference ref) const
{
    return pimpl_->get_event_output_value(type, ref);
}

void slave_simulator::signal_input_discontinuity(variable_type type,
    value_reference ref)
{
    pimpl_->signal_input_discontinuity(type, ref);
}

void slave_simulator::set_input_sample_discontinuity_policy(variable_type type,
    value_reference ref,
    bool enabled)
{
    pimpl_->set_input_sample_discontinuity_policy(type, ref, enabled);
}

void slave_simulator::set_input_modifier_replay_safe(variable_type type,
    value_reference ref,
    bool enabled)
{
    pimpl_->set_input_modifier_replay_safe(type, ref, enabled);
}

void slave_simulator::expose_for_getting(variable_type type,
    value_reference ref)
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

variable_value slave_simulator::get_variable_value(variable_type type,
    value_reference ref) const
{
    return pimpl_->get_variable_value(type, ref);
}

void slave_simulator::expose_for_setting(variable_type type,
    value_reference ref)
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

void slave_simulator::set_variable_value(variable_type type,
    value_reference ref,
    const variable_value& value)
{
    pimpl_->set_variable_value(type, ref, value);
}

void slave_simulator::stage_clocked_input(variable_type type,
    value_reference ref,
    const variable_value& value)
{
    pimpl_->stage_clocked_input(type, ref, value);
}

void slave_simulator::set_real_input_modifier(
    value_reference ref, std::function<double(double, duration)> modifier)
{
    pimpl_->set_real_input_modifier(ref, modifier);
}

void slave_simulator::set_integer_input_modifier(
    value_reference ref, std::function<int(int, duration)> modifier)
{
    pimpl_->set_integer_input_modifier(ref, modifier);
}

void slave_simulator::set_boolean_input_modifier(
    value_reference ref, std::function<bool(bool, duration)> modifier)
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
    value_reference ref, std::function<double(double, duration)> modifier)
{
    pimpl_->set_real_output_modifier(ref, modifier);
}

void slave_simulator::set_integer_output_modifier(
    value_reference ref, std::function<int(int, duration)> modifier)
{
    pimpl_->set_integer_output_modifier(ref, modifier);
}

void slave_simulator::set_boolean_output_modifier(
    value_reference ref, std::function<bool(bool, duration)> modifier)
{
    pimpl_->set_boolean_output_modifier(ref, modifier);
}

void slave_simulator::set_string_output_modifier(
    value_reference ref,
    std::function<std::string(std::string_view, duration)> modifier)
{
    pimpl_->set_string_output_modifier(ref, modifier);
}

void slave_simulator::set_variable_input_modifier(variable_type type,
    value_reference ref,
    variable_modifier modifier)
{
    pimpl_->set_variable_input_modifier(type, ref, std::move(modifier));
}

void slave_simulator::set_variable_output_modifier(variable_type type,
    value_reference ref,
    variable_modifier modifier)
{
    pimpl_->set_variable_output_modifier(type, ref, std::move(modifier));
}

std::unordered_set<value_reference>&
slave_simulator::get_modified_real_variables() const
{
    return pimpl_->get_modified_real_variables();
}

std::unordered_set<value_reference>&
slave_simulator::get_modified_integer_variables() const
{
    return pimpl_->get_modified_integer_variables();
}

std::unordered_set<value_reference>&
slave_simulator::get_modified_boolean_variables() const
{
    return pimpl_->get_modified_boolean_variables();
}

std::unordered_set<value_reference>&
slave_simulator::get_modified_string_variables() const
{
    return pimpl_->get_modified_string_variables();
}

std::vector<typed_value_reference>
slave_simulator::get_modified_variables() const
{
    return pimpl_->get_modified_variables();
}

bool slave_simulator::has_variable_modifiers() const
{
    return pimpl_->has_variable_modifiers();
}

bool slave_simulator::supports_automatic_step_replay() const
{
    return pimpl_->supports_automatic_step_replay();
}

void slave_simulator::set_max_event_iterations(std::size_t limit)
{
    pimpl_->set_max_event_iterations(limit);
}

void slave_simulator::setup(time_point startTime,
    std::optional<time_point> stopTime,
    std::optional<double> relativeTolerance)
{
    pimpl_->setup(startTime, stopTime, relativeTolerance);
}

void slave_simulator::do_iteration() { pimpl_->do_iteration(); }

void slave_simulator::start_simulation() { pimpl_->start_simulation(); }

bool slave_simulator::apply_pending_step_inputs(duration deltaT)
{
    return pimpl_->apply_pending_step_inputs(deltaT);
}

step_result slave_simulator::do_step(time_point currentT, duration deltaT)
{
    return pimpl_->do_step(currentT, deltaT);
}

step_result_info slave_simulator::do_step_with_info(time_point currentT,
    duration deltaT)
{
    return pimpl_->do_step_with_info(currentT, deltaT);
}

void slave_simulator::handle_events() { pimpl_->handle_events(); }

event_handling_status
slave_simulator::get_event_handling_status() const noexcept
{
    return pimpl_->get_event_handling_status();
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

serialization::node
slave_simulator::export_state(state_index stateIndex) const
{
    return pimpl_->export_state(stateIndex);
}

simulator::state_index
slave_simulator::import_state(const serialization::node& exportedState)
{
    return pimpl_->import_state(exportedState);
}

} // namespace cosim
