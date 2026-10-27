/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/fmi/glue.hpp"

#include "cosim/error.hpp"
#include "cosim/exception.hpp"

#include <algorithm>
#include <cassert>
#include <unordered_set>
#include <utility>


namespace cosim
{
namespace fmi
{

namespace
{
const char* nonnull(const char* value)
{
    return value == nullptr ? "" : value;
}

std::optional<std::string> optional_string(const char* value)
{
    return value == nullptr ? std::nullopt
                            : std::optional<std::string>(value);
}
} // namespace


variable_type to_variable_type(fmi1_base_type_enu_t t)
{
    switch (t) {
        case fmi1_base_type_real: return variable_type::real;
        case fmi1_base_type_int: return variable_type::integer;
        case fmi1_base_type_bool: return variable_type::boolean;
        case fmi1_base_type_str: return variable_type::string;
        case fmi1_base_type_enum: return variable_type::enumeration;
    }
    COSIM_PANIC();
}


variable_type to_variable_type(fmi2_base_type_enu_t t)
{
    switch (t) {
        case fmi2_base_type_real: return variable_type::real;
        case fmi2_base_type_int: return variable_type::integer;
        case fmi2_base_type_bool: return variable_type::boolean;
        case fmi2_base_type_str: return variable_type::string;
        case fmi2_base_type_enum: return variable_type::enumeration;
    }
    COSIM_PANIC();
}


variable_causality to_variable_causality(
    fmi1_causality_enu_t c,
    fmi1_variability_enu_t v)
{
    switch (c) {
        case fmi1_causality_enu_input:
            return (v == fmi1_variability_enu_parameter)
                ? variable_causality::parameter
                : variable_causality::input;
        case fmi1_causality_enu_output:
            return variable_causality::output;
        case fmi1_causality_enu_internal:
        case fmi1_causality_enu_none:
            return variable_causality::local;
        case fmi1_causality_enu_unknown:
            COSIM_PANIC();
    }
    COSIM_PANIC();
}


variable_causality to_variable_causality(fmi2_causality_enu_t c)
{
    switch (c) {
        case fmi2_causality_enu_parameter: return variable_causality::parameter;
        case fmi2_causality_enu_calculated_parameter: return variable_causality::calculated_parameter;
        case fmi2_causality_enu_input: return variable_causality::input;
        case fmi2_causality_enu_output: return variable_causality::output;
        case fmi2_causality_enu_local:
        case fmi2_causality_enu_independent: return variable_causality::local;
        case fmi2_causality_enu_unknown:
            COSIM_PANIC();
    }
    COSIM_PANIC();
}


variable_variability to_variable_variability(fmi1_variability_enu_t v)
{
    switch (v) {
        case fmi1_variability_enu_constant: return variable_variability::constant;
        case fmi1_variability_enu_parameter: return variable_variability::fixed;
        case fmi1_variability_enu_discrete: return variable_variability::discrete;
        case fmi1_variability_enu_continuous: return variable_variability::continuous;
        case fmi1_variability_enu_unknown:
            COSIM_PANIC();
    }
    COSIM_PANIC();
}


variable_variability to_variable_variability(fmi2_variability_enu_t v)
{
    switch (v) {
        case fmi2_variability_enu_constant: return variable_variability::constant;
        case fmi2_variability_enu_fixed: return variable_variability::fixed;
        case fmi2_variability_enu_tunable: return variable_variability::tunable;
        case fmi2_variability_enu_discrete: return variable_variability::discrete;
        case fmi2_variability_enu_continuous: return variable_variability::continuous;
        case fmi2_variability_enu_unknown:
            COSIM_PANIC();
    }
    COSIM_PANIC();
}


std::optional<scalar_value> fmi1_to_start_value(fmi1_import_variable_t* fmiVariable, variable_type type)
{
    if (!fmi1_import_get_variable_has_start(fmiVariable)) {
        return std::nullopt;
    }
    switch (type) {
        case variable_type::real:
            return fmi1_import_get_real_variable_start(fmi1_import_get_variable_as_real(fmiVariable));
        case variable_type::integer:
            return fmi1_import_get_integer_variable_start(fmi1_import_get_variable_as_integer(fmiVariable));
        case variable_type::boolean:
            return fmi1_import_get_boolean_variable_start(fmi1_import_get_variable_as_boolean(fmiVariable)) ? true : false;
        case variable_type::string:
            return std::string(fmi1_import_get_string_variable_start(fmi1_import_get_variable_as_string(fmiVariable)));
        case variable_type::enumeration:
            return enumeration_value{fmi1_import_get_enum_variable_start(
                fmi1_import_get_variable_as_enum(fmiVariable))};
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


variable_description to_variable_description(fmi1_import_variable_t* fmiVariable)
{
    assert(fmiVariable != nullptr);
    const auto fmiVariability = fmi1_import_get_variability(fmiVariable);
    const auto type = to_variable_type(fmi1_import_get_variable_base_type(fmiVariable));
    variable_description description{};
    description.name = fmi1_import_get_variable_name(fmiVariable);
    description.reference = fmi1_import_get_variable_vr(fmiVariable);
    description.type = type;
    description.causality = to_variable_causality(fmi1_import_get_causality(fmiVariable), fmiVariability);
    description.variability = to_variable_variability(fmiVariability);
    description.start = fmi1_to_start_value(fmiVariable, type);
    if (const auto declaredType =
            fmi1_import_get_variable_declared_type(fmiVariable)) {
        description.declared_type = nonnull(fmi1_import_get_type_name(declaredType));
    }
    if (type == variable_type::enumeration) {
        const auto enumeration = fmi1_import_get_variable_as_enum(fmiVariable);
        description.quantity =
            optional_string(fmi1_import_get_enum_variable_quantity(enumeration));
        description.min = enumeration_value{
            fmi1_import_get_enum_variable_min(enumeration)};
        description.max = enumeration_value{
            fmi1_import_get_enum_variable_max(enumeration)};
    }
    return description;
}

std::vector<variable_type_definition>
to_type_definitions(fmi1_import_t* fmu)
{
    const auto typeDefinitions = fmi1_import_get_type_definitions(fmu);
    if (typeDefinitions == nullptr) return {};
    std::vector<variable_type_definition> result;
    const auto count = fmi1_import_get_type_definition_number(typeDefinitions);
    result.reserve(count);
    for (unsigned int i = 0; i < count; ++i) {
        const auto typeDefinition = fmi1_import_get_typedef(typeDefinitions, i);
        if (typeDefinition == nullptr) {
            throw error(make_error_code(errc::bad_file),
                "FMI 1.0 model contains an invalid declared type definition");
        }
        variable_type_definition definition;
        definition.name = nonnull(fmi1_import_get_type_name(typeDefinition));
        definition.description = nonnull(fmi1_import_get_type_description(typeDefinition));
        definition.quantity = optional_string(fmi1_import_get_type_quantity(typeDefinition));
        definition.type = to_variable_type(fmi1_import_get_base_type(typeDefinition));
        if (definition.type == variable_type::enumeration) {
            const auto enumeration = fmi1_import_get_type_as_enum(typeDefinition);
            if (enumeration == nullptr || definition.name.empty()) {
                throw error(make_error_code(errc::bad_file),
                    "FMI 1.0 Enumeration declared type is incomplete");
            }
            const auto itemCount = fmi1_import_get_enum_type_size(enumeration);
            if (itemCount == 0) {
                throw error(make_error_code(errc::bad_file),
                    "FMI 1.0 Enumeration declared type '" + definition.name +
                        "' has no items");
            }
            std::unordered_set<std::string> names;
            definition.enumeration_items.reserve(itemCount);
            for (unsigned int item = 1; item <= itemCount; ++item) {
                const auto name =
                    nonnull(fmi1_import_get_enum_type_item_name(enumeration, item));
                if (name[0] == '\0' || !names.emplace(name).second) {
                    throw error(make_error_code(errc::bad_file),
                        "FMI 1.0 Enumeration declared type '" + definition.name +
                            "' contains duplicate or empty item metadata");
                }
                definition.enumeration_items.push_back(enumeration_item{
                    name, static_cast<std::int64_t>(item),
                    nonnull(fmi1_import_get_enum_type_item_description(enumeration, item))});
            }
            definition.min = enumeration_value{
                fmi1_import_get_enum_type_min(enumeration)};
            definition.max = enumeration_value{
                fmi1_import_get_enum_type_max(enumeration)};
        } else if (definition.type == variable_type::integer) {
            if (const auto integer = fmi1_import_get_type_as_int(typeDefinition)) {
                definition.min = static_cast<std::int64_t>(
                    fmi1_import_get_integer_type_min(integer));
                definition.max = static_cast<std::int64_t>(
                    fmi1_import_get_integer_type_max(integer));
            }
        }
        result.push_back(std::move(definition));
    }
    return result;
}


std::optional<scalar_value> fmi2_to_start_value(fmi2_import_variable_t* fmiVariable, variable_type type)
{
    if (!fmi2_import_get_variable_has_start(fmiVariable)) {
        return std::nullopt;
    }
    switch (type) {
        case variable_type::real:
            return fmi2_import_get_real_variable_start(fmi2_import_get_variable_as_real(fmiVariable));
        case variable_type::integer:
            return fmi2_import_get_integer_variable_start(fmi2_import_get_variable_as_integer(fmiVariable));
        case variable_type::boolean:
            return fmi2_import_get_boolean_variable_start(fmi2_import_get_variable_as_boolean(fmiVariable)) ? true : false;
        case variable_type::string:
            return std::string(fmi2_import_get_string_variable_start(fmi2_import_get_variable_as_string(fmiVariable)));
        case variable_type::enumeration:
            return enumeration_value{
                fmi2_import_get_enum_variable_start(
                    fmi2_import_get_variable_as_enum(fmiVariable))};
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


variable_description to_variable_description(fmi2_import_variable_t* fmiVariable)
{
    assert(fmiVariable != nullptr);
    const auto type = to_variable_type(fmi2_import_get_variable_base_type(fmiVariable));
    variable_description description{};
    description.name = nonnull(fmi2_import_get_variable_name(fmiVariable));
    description.description =
        nonnull(fmi2_import_get_variable_description(fmiVariable));
    description.reference = fmi2_import_get_variable_vr(fmiVariable);
    description.type = type;
    description.causality = to_variable_causality(fmi2_import_get_causality(fmiVariable));
    description.variability = to_variable_variability(fmi2_import_get_variability(fmiVariable));
    description.start = fmi2_to_start_value(fmiVariable, type);
    if (const auto declaredType =
            fmi2_import_get_variable_declared_type(fmiVariable)) {
        description.declared_type =
            nonnull(fmi2_import_get_type_name(declaredType));
    }
    if (type == variable_type::enumeration) {
        const auto enumeration =
            fmi2_import_get_variable_as_enum(fmiVariable);
        description.quantity =
            optional_string(fmi2_import_get_enum_variable_quantity(enumeration));
    }
    return description;
}

std::vector<variable_type_definition>
to_type_definitions(fmi2_import_t* fmu)
{
    const auto typeDefinitions = fmi2_import_get_type_definitions(fmu);
    if (typeDefinitions == nullptr) {
        throw error(make_error_code(errc::bad_file),
            "FMI 2.0 model has no readable TypeDefinitions element");
    }
    std::vector<variable_type_definition> result;
    const auto count = fmi2_import_get_type_definition_number(typeDefinitions);
    result.reserve(count);
    for (unsigned int i = 0; i < count; ++i) {
        const auto typeDefinition = fmi2_import_get_typedef(typeDefinitions, i);
        if (typeDefinition == nullptr) {
            throw error(make_error_code(errc::bad_file),
                "FMI 2.0 model contains an invalid declared type definition");
        }
        variable_type_definition definition;
        definition.name = nonnull(fmi2_import_get_type_name(typeDefinition));
        definition.description = nonnull(fmi2_import_get_type_description(typeDefinition));
        definition.quantity = optional_string(fmi2_import_get_type_quantity(typeDefinition));
        definition.type = to_variable_type(fmi2_import_get_base_type(typeDefinition));
        if (definition.type == variable_type::enumeration) {
            const auto enumeration = fmi2_import_get_type_as_enum(typeDefinition);
            if (enumeration == nullptr || definition.name.empty()) {
                throw error(make_error_code(errc::bad_file),
                    "FMI 2.0 Enumeration declared type is incomplete");
            }
            const auto itemCount = fmi2_import_get_enum_type_size(enumeration);
            if (itemCount == 0) {
                throw error(make_error_code(errc::bad_file),
                    "FMI 2.0 Enumeration declared type '" + definition.name +
                        "' has no items");
            }
            std::unordered_set<std::string> itemNames;
            std::unordered_set<int> itemValues;
            definition.enumeration_items.reserve(itemCount);
            for (unsigned int item = 1; item <= itemCount; ++item) {
                const auto name = nonnull(
                    fmi2_import_get_enum_type_item_name(enumeration, item));
                const auto value =
                    fmi2_import_get_enum_type_item_value(enumeration, item);
                if (name[0] == '\0' || !itemNames.emplace(name).second ||
                    !itemValues.emplace(value).second) {
                    throw error(make_error_code(errc::bad_file),
                        "FMI 2.0 Enumeration declared type '" + definition.name +
                            "' contains duplicate or empty item metadata");
                }
                definition.enumeration_items.push_back(enumeration_item{
                    name, static_cast<std::int64_t>(value),
                    nonnull(fmi2_import_get_enum_type_item_description(enumeration, item))});
            }
            const auto less = [](const auto& lhs, const auto& rhs) {
                return lhs.value < rhs.value;
            };
            const auto minItem = std::min_element(
                definition.enumeration_items.begin(), definition.enumeration_items.end(), less);
            const auto maxItem = std::max_element(
                definition.enumeration_items.begin(), definition.enumeration_items.end(), less);
            definition.min = enumeration_value{minItem->value};
            definition.max = enumeration_value{maxItem->value};
        } else if (definition.type == variable_type::integer) {
            if (const auto integer = fmi2_import_get_type_as_int(typeDefinition)) {
                definition.min = static_cast<std::int64_t>(
                    fmi2_import_get_integer_type_min(integer));
                definition.max = static_cast<std::int64_t>(
                    fmi2_import_get_integer_type_max(integer));
            }
        }
        result.push_back(std::move(definition));
    }
    return result;
}


} // namespace fmi
} // namespace cosim
