/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "glue_internal.hpp"

#include "cosim/error.hpp"
#include "cosim/exception.hpp"
#include "cosim/fmi/glue.hpp"
#include "cosim/model_description.hpp"

#include <FMI/fmi_import_context.h>
#include <FMI1/fmi1_enums.h>
#include <FMI1/fmi1_import.h>
#include <FMI1/fmi1_import_type.h>
#include <FMI1/fmi1_import_unit.h>
#include <FMI1/fmi1_import_variable.h>
#include <cassert>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>


namespace cosim::fmi
{

using glue_detail::nonnull;
using glue_detail::optional_string;

namespace
{
variable_alias_kind to_alias_kind(fmi1_variable_alias_kind_enu_t kind)
{
    switch (kind) {
        case fmi1_variable_is_not_alias: return variable_alias_kind::no_alias;
        case fmi1_variable_is_alias: return variable_alias_kind::alias;
        case fmi1_variable_is_negated_alias: return variable_alias_kind::negated_alias;
    }
    COSIM_PANIC();
}
} // namespace

variable_type to_variable_type(fmi1_base_type_enu_t type)
{
    switch (type) {
        case fmi1_base_type_real: return variable_type::real;
        case fmi1_base_type_int: return variable_type::integer;
        case fmi1_base_type_bool: return variable_type::boolean;
        case fmi1_base_type_str: return variable_type::string;
        case fmi1_base_type_enum: return variable_type::enumeration;
    }
    COSIM_PANIC();
}

variable_causality to_variable_causality(
    fmi1_causality_enu_t causality,
    fmi1_variability_enu_t variability)
{
    switch (causality) {
        case fmi1_causality_enu_input:
            return (variability == fmi1_variability_enu_parameter)
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

variable_variability to_variable_variability(fmi1_variability_enu_t variability)
{
    switch (variability) {
        case fmi1_variability_enu_constant: return variable_variability::constant;
        case fmi1_variability_enu_parameter: return variable_variability::fixed;
        case fmi1_variability_enu_discrete: return variable_variability::discrete;
        case fmi1_variability_enu_continuous: return variable_variability::continuous;
        case fmi1_variability_enu_unknown:
            COSIM_PANIC();
    }
    COSIM_PANIC();
}

std::optional<scalar_value> fmi1_to_start_value(fmi1_import_variable_t* fmiVariable, variable_type type)
{
    if (fmi1_import_get_variable_has_start(fmiVariable) == 0) {
        return std::nullopt;
    }
    switch (type) {
        case variable_type::real:
            return fmi1_import_get_real_variable_start(fmi1_import_get_variable_as_real(fmiVariable));
        case variable_type::integer:
            return fmi1_import_get_integer_variable_start(fmi1_import_get_variable_as_integer(fmiVariable));
        case variable_type::boolean:
            return fmi1_import_get_boolean_variable_start(fmi1_import_get_variable_as_boolean(fmiVariable)) != 0;
        case variable_type::string:
            return std::string(fmi1_import_get_string_variable_start(fmi1_import_get_variable_as_string(fmiVariable)));
        case variable_type::enumeration:
            return enumeration_value{
                fmi1_import_get_enum_variable_start(
                    fmi1_import_get_variable_as_enum(fmiVariable)),
            };
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
    description.description =
        nonnull(fmi1_import_get_variable_description(fmiVariable));
    description.reference = fmi1_import_get_variable_vr(fmiVariable);
    description.type = type;
    description.causality = to_variable_causality(fmi1_import_get_causality(fmiVariable), fmiVariability);
    description.variability = to_variable_variability(fmiVariability);
    description.start = fmi1_to_start_value(fmiVariable, type);
    description.alias_kind =
        to_alias_kind(fmi1_import_get_variable_alias_kind(fmiVariable));
    if (auto* const declaredType =
            fmi1_import_get_variable_declared_type(fmiVariable)) {
        description.declared_type = nonnull(fmi1_import_get_type_name(declaredType));
    }
    if (type == variable_type::real) {
        auto* const real = fmi1_import_get_variable_as_real(fmiVariable);
        description.quantity =
            optional_string(fmi1_import_get_real_variable_quantity(real));
        if (auto* const unit = fmi1_import_get_real_variable_unit(real)) {
            description.unit = nonnull(fmi1_import_get_unit_name(unit));
        }
        if (auto* const display = fmi1_import_get_real_variable_display_unit(real)) {
            description.display_unit =
                nonnull(fmi1_import_get_display_unit_name(display));
        }
        description.min = fmi1_import_get_real_variable_min(real);
        description.max = fmi1_import_get_real_variable_max(real);
        description.nominal = fmi1_import_get_real_variable_nominal(real);
        description.relative_quantity =
            fmi1_import_get_real_variable_relative_quantity(real) != 0;
    } else if (type == variable_type::integer) {
        auto* const integer = fmi1_import_get_variable_as_integer(fmiVariable);
        description.quantity =
            optional_string(fmi1_import_get_integer_variable_quantity(integer));
        description.min = fmi1_import_get_integer_variable_min(integer);
        description.max = fmi1_import_get_integer_variable_max(integer);
    } else if (type == variable_type::enumeration) {
        auto* const enumeration = fmi1_import_get_variable_as_enum(fmiVariable);
        description.quantity =
            optional_string(fmi1_import_get_enum_variable_quantity(enumeration));
        description.min = enumeration_value{
            fmi1_import_get_enum_variable_min(enumeration),
        };
        description.max = enumeration_value{
            fmi1_import_get_enum_variable_max(enumeration),
        };
    }
    return description;
}

namespace
{

void add_enumeration_type_metadata(
    variable_type_definition& definition,
    fmi1_import_enumeration_typedef_t* enumeration)
{
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
        const auto* const name =
            nonnull(fmi1_import_get_enum_type_item_name(enumeration, item));
        if (*name == '\0' || !names.emplace(name).second) {
            throw error(make_error_code(errc::bad_file),
                "FMI 1.0 Enumeration declared type '" + definition.name +
                    "' contains duplicate or empty item metadata");
        }
        definition.enumeration_items.push_back(enumeration_item{
            name,
            static_cast<std::int64_t>(item),
            nonnull(fmi1_import_get_enum_type_item_description(enumeration, item)),
        });
    }
    definition.min = enumeration_value{
        fmi1_import_get_enum_type_min(enumeration),
    };
    definition.max = enumeration_value{
        fmi1_import_get_enum_type_max(enumeration),
    };
}

void add_real_type_metadata(
    variable_type_definition& definition,
    fmi1_import_real_typedef_t* real)
{
    if (real == nullptr) {
        return;
    }
    if (auto* const unit = fmi1_import_get_real_type_unit(real)) {
        definition.unit = nonnull(fmi1_import_get_unit_name(unit));
        // FMI Library reads the display unit through a pointer that is null
        // when no unit is declared. An empty name means "not declared".
        if (auto* const display = fmi1_import_get_type_display_unit(real)) {
            const std::string name =
                nonnull(fmi1_import_get_display_unit_name(display));
            if (!name.empty()) {
                definition.display_unit = name;
            }
        }
    }
    definition.min = fmi1_import_get_real_type_min(real);
    definition.max = fmi1_import_get_real_type_max(real);
    definition.nominal = fmi1_import_get_real_type_nominal(real);
    definition.relative_quantity =
        fmi1_import_get_real_type_is_relative_quantity(real) != 0;
}

} // namespace

std::vector<variable_type_definition>
to_type_definitions(fmi1_import_t* fmu)
{
    auto* const typeDefinitions = fmi1_import_get_type_definitions(fmu);
    if (typeDefinitions == nullptr) {
        return {};
    }
    std::vector<variable_type_definition> result;
    const auto count = fmi1_import_get_type_definition_number(typeDefinitions);
    result.reserve(count);
    for (unsigned int i = 0; i < count; ++i) {
        auto* const typeDefinition = fmi1_import_get_typedef(typeDefinitions, i);
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
            add_enumeration_type_metadata(
                definition, fmi1_import_get_type_as_enum(typeDefinition));
        } else if (definition.type == variable_type::real) {
            add_real_type_metadata(
                definition, fmi1_import_get_type_as_real(typeDefinition));
        } else if (definition.type == variable_type::integer) {
            if (auto* const integer = fmi1_import_get_type_as_int(typeDefinition)) {
                definition.min = fmi1_import_get_integer_type_min(integer);
                definition.max = fmi1_import_get_integer_type_max(integer);
            }
        }
        result.push_back(std::move(definition));
    }
    return result;
}

std::vector<unit_definition> to_unit_definitions(fmi1_import_t* fmu)
{
    auto* const list = fmi1_import_get_unit_definitions(fmu);
    if (list == nullptr) {
        return {};
    }
    const auto count = fmi1_import_get_unit_definitions_number(list);
    std::vector<unit_definition> result;
    result.reserve(count);
    for (unsigned int i = 0; i < count; ++i) {
        auto* const unit = fmi1_import_get_unit(list, i);
        if (unit == nullptr) {
            throw error(make_error_code(errc::bad_file),
                "FMI 1.0 model contains an invalid unit definition");
        }
        unit_definition converted;
        converted.name = nonnull(fmi1_import_get_unit_name(unit));
        const auto displayCount = fmi1_import_get_unit_display_unit_number(unit);
        converted.display_units.reserve(displayCount);
        for (unsigned int j = 0; j < displayCount; ++j) {
            auto* const display = fmi1_import_get_unit_display_unit(unit, j);
            if (display == nullptr) {
                throw error(make_error_code(errc::bad_file),
                    "FMI 1.0 model contains an invalid display unit");
            }
            converted.display_units.push_back({
                nonnull(fmi1_import_get_display_unit_name(display)),
                fmi1_import_get_display_unit_gain(display),
                fmi1_import_get_display_unit_offset(display),
                false,
            });
        }
        result.push_back(std::move(converted));
    }
    return result;
}

} // namespace cosim::fmi
