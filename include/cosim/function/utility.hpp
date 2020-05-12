/**
 *  \file
 *  Utilities for `function` implementers.
 */
#ifndef COSIM_FUNCTION_UTILITY_HPP
#define COSIM_FUNCTION_UTILITY_HPP

#include <cosim/function/description.hpp>

#include <stdexcept>
#include <type_traits>
#include <unordered_map>


namespace cosim
{


/**
 *  Retrieves a parameter value from a parameter value map.
 *
 *  This is a convenience function meant to aid in the implementation of
 *  `function_type::instantiate()` by providing a simple and safe way
 *  to extract parameter values from the map passed to this function.
 *
 *  \param functionTypeDescription
 *      The function type description (to get the list of parameter
 *      definitions).
 *  \param parameterValues
 *      A mapping from parameter indexes to parameter values.
 *  \param parameterIndex
 *      The index of the parameter whose value should be retrieved.
 */
template<typename T>
T get_function_parameter(
    const function_type_description& functionTypeDescription,
    const function_parameter_value_map& parameterValues,
    int parameterIndex)
{
    const auto& description = functionTypeDescription.parameters.at(parameterIndex);
    const auto it = parameterValues.find(parameterIndex);
    if (it == parameterValues.end()) {
        return std::get<T>(description.default_value);
    }
    const auto value = std::get<T>(it->second);
    if constexpr (std::is_arithmetic_v<T>) {
        if ((description.min_value && value < std::get<T>(*description.min_value)) ||
            (description.max_value && value > std::get<T>(*description.max_value))) {
            throw std::domain_error(
                "Parameter '" + description.name + "' is out of bounds");
        }
    }
    return value;
}


/**
 *  Returns a `function_description` with the same contents as the
 *  `function_description` part of `functionTypeDescription`, but with
 *  all placeholders replaced by actual parameter values.
 *
 *  The `parameterValues` map *must* contain values for all placeholders
 *  in `functionTypeDescription`.  Otherwise, an exception is thrown.
 */
function_description substitute_function_parameters(
    const function_type_description& functionTypeDescription,
    const function_parameter_value_map& parameterValues);


} // namespace cosim
#endif // header guard
