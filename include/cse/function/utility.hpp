/**
 *  \file
 *  Utilities for `function` implementers.
 */
#ifndef CSE_FUNCTION_UTILITY_HPP
#define CSE_FUNCTION_UTILITY_HPP

#include <cse/function/description.hpp>

#include <stdexcept>
#include <type_traits>
#include <unordered_map>


namespace cse
{


/**
 *  Retrieves a parameter value from a parameter value map.
 *
 *  This is a convenience function meant to aid in the implementation of
 *  `function_type::instantiate()` by providing a simple and safe way
 *  to extract parameter values from the map passed to this function.
 *
 *  \param functionDescription
 *      The function description (to get the list of parameter
 *      definitions).
 *  \param parameterValues
 *      A mapping from parameter indexes to parameter values.
 *  \param parameterIndex
 *      The index of the parameter whose value should be retrieved.
 */
template<typename T>
T get_function_parameter(
    const function_type_description& functionDescription,
    const std::unordered_map<int, function_parameter_value> parameterValues,
    int parameterIndex)
{
    const auto& description = functionDescription.parameters.at(parameterIndex);
    const auto it = parameterValues.find(parameterIndex);
    if (it == parameterValues.end()) {
        return std::get<T>(description.default_value);
    }
    const auto value = std::get<T>(it->second);
    if constexpr (std::is_arithmetic_v<T>) {
        if ((description.min_value && value < std::get<T>(*description.min_value)) ||
            (description.max_value && value > std::get<T>(*description.max_value))) {
            throw std::domain_error(
                "Parameter " + functionDescription.name + ':' +
                description.name + " is out of bounds");
        }
    }
    return value;
}


} // namespace cse
#endif // header guard
