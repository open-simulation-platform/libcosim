/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/function/utility.hpp"

#include "cosim/function/description.hpp"
#include "cosim/model_description.hpp"

#include <boost/lexical_cast.hpp>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>


namespace cosim
{
namespace
{
template<typename T>
class replace_placeholder
{
public:
    replace_placeholder(
        const function_parameter_value_map& parameterValues,
        const std::vector<function_parameter_description>& parameterDescriptions)
        : parameterValues_(parameterValues)
        , parameterDescriptions_(parameterDescriptions)
    { }

    T operator()(const T& value) { return value; }

    T operator()(const function_parameter_placeholder& placeholder)
    {
        const auto index = placeholder.parameter_index;
        if (index < 0 ||
            static_cast<std::size_t>(index) >= parameterDescriptions_.size()) {
            throw std::out_of_range(
                "Invalid parameter index in placeholder: " +
                std::to_string(index));
        }
        const auto& descr = parameterDescriptions_.at(static_cast<std::size_t>(index));

        auto const valueIt = parameterValues_.find(placeholder.parameter_index);
        if (valueIt == parameterValues_.end()) {
            return get_value(descr.default_value, descr);
        }
        const auto value = get_value(valueIt->second, descr);
        if constexpr (std::is_arithmetic_v<T>) {
            if (descr.min_value && value < std::get<T>(*descr.min_value)) {
                throw std::domain_error(
                    "Value of parameter '" + descr.name + "' too small: " +
                    boost::lexical_cast<std::string>(value));
            }
            if (descr.max_value && value > std::get<T>(*descr.max_value)) {
                throw std::domain_error(
                    "Value of parameter '" + descr.name + "' too large: " +
                    boost::lexical_cast<std::string>(value));
            }
        }
        return value;
    }

private:
    static T get_value(
        const function_parameter_value& value,
        const function_parameter_description& description)
    {
        if (const auto* const result = std::get_if<T>(&value)) {
            return *result;
        }
        throw std::logic_error(
            "Parameter '" + description.name + "': Illegal value type");
    }

    const function_parameter_value_map& parameterValues_; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members): The visitor borrows parameter values.
    const std::vector<function_parameter_description>& parameterDescriptions_; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members): The visitor borrows parameter descriptions.
};


std::uint64_t replace_dimension_placeholder(
    const function_io_dimension& dimension,
    const function_parameter_value_map& parameterValues,
    const std::vector<function_parameter_description>& parameterDescriptions)
{
    if (const auto* const concrete = std::get_if<std::uint64_t>(&dimension)) {
        return *concrete;
    }
    const auto value = replace_placeholder<int>(
        parameterValues,
        parameterDescriptions)(
        std::get<function_parameter_placeholder>(dimension));
    if (value < 0) {
        throw std::domain_error("Function I/O dimensions cannot be negative");
    }
    return static_cast<std::uint64_t>(value);
}
} // namespace


function_description substitute_function_parameters(
    const function_type_description& functionTypeDescription,
    const function_parameter_value_map& parameterValues)
{
    auto functionDescription = function_description(functionTypeDescription); // NOLINT(cppcoreguidelines-slicing): Instantiate the base description without type parameters.
    for (auto& group : functionDescription.io_groups) {
        group.count = std::visit(
            replace_placeholder<int>(parameterValues, functionTypeDescription.parameters),
            group.count);

        for (auto& connection : group.ios) {
            connection.count = std::visit(
                replace_placeholder<int>(parameterValues, functionTypeDescription.parameters),
                connection.count);
            connection.type = std::visit(
                replace_placeholder<variable_type>(parameterValues, functionTypeDescription.parameters),
                connection.type);
            for (auto& dimension : connection.dimensions) {
                dimension = replace_dimension_placeholder(
                    dimension,
                    parameterValues,
                    functionTypeDescription.parameters);
            }
        }
    }
    return functionDescription;
}

} // namespace cosim
