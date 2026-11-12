/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_FMI_ENUMERATION_HPP
#define COSIM_FMI_ENUMERATION_HPP

#include "cosim/error.hpp"
#include "cosim/model_description.hpp"

#include <algorithm>
#include <limits>
#include <string>

namespace cosim
{
namespace fmi
{
namespace detail
{

inline const variable_description* find_variable_description(
    const model_description& model,
    variable_type type,
    value_reference reference)
{
    const auto variable = std::find_if(
        model.variables.begin(),
        model.variables.end(),
        [type, reference](const auto& candidate) {
            return candidate.type == type && candidate.reference == reference;
        });
    return variable == model.variables.end() ? nullptr : &*variable;
}

inline const variable_type_definition* find_type_definition(
    const model_description& model,
    const std::string& name)
{
    const auto definition = std::find_if(
        model.type_definitions.begin(),
        model.type_definitions.end(),
        [&name](const auto& candidate) { return candidate.name == name; });
    return definition == model.type_definitions.end() ? nullptr : &*definition;
}

inline void validate_enumeration_value(
    const model_description& model,
    const variable_description& variable,
    enumeration_value value,
    const std::string& instanceName)
{
    if (variable.min && value.value < std::get<enumeration_value>(*variable.min).value) {
        throw error(
            make_error_code(errc::model_error),
            instanceName + ": Enumeration value " + std::to_string(value.value) +
                " is below the lower bound of variable '" + variable.name + "'");
    }
    if (variable.max && value.value > std::get<enumeration_value>(*variable.max).value) {
        throw error(
            make_error_code(errc::model_error),
            instanceName + ": Enumeration value " + std::to_string(value.value) +
                " is above the upper bound of variable '" + variable.name + "'");
    }
    if (!variable.declared_type) {
        throw error(
            make_error_code(errc::model_error),
            instanceName + ": Enumeration variable '" + variable.name +
                "' has no declared type");
    }
    const auto* definition = find_type_definition(model, *variable.declared_type);
    if (definition == nullptr || definition->type != variable_type::enumeration) {
        throw error(
            make_error_code(errc::model_error),
            instanceName + ": Enumeration variable '" + variable.name +
                "' has an invalid declared type");
    }
    const auto item = std::find_if(
        definition->enumeration_items.begin(),
        definition->enumeration_items.end(),
        [value](const auto& candidate) { return candidate.value == value.value; });
    if (item == definition->enumeration_items.end()) {
        throw error(
            make_error_code(errc::model_error),
            instanceName + ": Enumeration value " + std::to_string(value.value) +
                " is not declared for variable '" + variable.name + "'");
    }
}

inline void validate_enumeration_reference_value(
    const model_description& model,
    value_reference reference,
    enumeration_value value,
    const std::string& instanceName)
{
    bool found = false;
    for (const auto& variable : model.variables) {
        if (variable.type == variable_type::enumeration && variable.reference == reference) {
            found = true;
            validate_enumeration_value(model, variable, value, instanceName);
        }
    }
    if (!found) {
        throw error(make_error_code(errc::model_error),
            instanceName + ": value reference " + std::to_string(reference) +
                " is not an Enumeration variable");
    }
}

template<typename Integer>
void validate_legacy_enumeration_assignment(
    const model_description& model,
    value_reference reference,
    enumeration_value value,
    const std::string& instanceName,
    const char* version)
{
    detail::validate_enumeration_reference_value(model, reference, value, instanceName);
    if (value.value < (std::numeric_limits<Integer>::min)() ||
        value.value > (std::numeric_limits<Integer>::max)()) {
        throw error(make_error_code(errc::model_error),
            instanceName + ": Enumeration value is outside the FMI " +
                version + " Integer range");
    }
}

} // namespace detail
} // namespace fmi
} // namespace cosim
#endif
