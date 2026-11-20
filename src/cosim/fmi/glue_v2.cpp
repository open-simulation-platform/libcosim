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

#include <boost/range/algorithm/max_element.hpp>
#include <boost/range/algorithm/min_element.hpp>
#include <gsl/span>
#include <gsl/span_ext>

#include <FMI/fmi_import_context.h>
#include <FMI2/fmi2_enums.h>
#include <FMI2/fmi2_import.h>
#include <FMI2/fmi2_import_type.h>
#include <FMI2/fmi2_import_unit.h>
#include <FMI2/fmi2_import_variable.h>
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

variable_type to_variable_type(fmi2_base_type_enu_t type)
{
    switch (type) {
        case fmi2_base_type_real: return variable_type::real;
        case fmi2_base_type_int: return variable_type::integer;
        case fmi2_base_type_bool: return variable_type::boolean;
        case fmi2_base_type_str: return variable_type::string;
        case fmi2_base_type_enum: return variable_type::enumeration;
    }
    COSIM_PANIC();
}

variable_causality to_variable_causality(fmi2_causality_enu_t causality)
{
    switch (causality) {
        case fmi2_causality_enu_parameter: return variable_causality::parameter;
        case fmi2_causality_enu_calculated_parameter: return variable_causality::calculated_parameter;
        case fmi2_causality_enu_input: return variable_causality::input;
        case fmi2_causality_enu_output: return variable_causality::output;
        case fmi2_causality_enu_local: return variable_causality::local;
        case fmi2_causality_enu_independent: return variable_causality::independent;
        case fmi2_causality_enu_unknown:
            COSIM_PANIC();
    }
    COSIM_PANIC();
}

variable_variability to_variable_variability(fmi2_variability_enu_t variability)
{
    switch (variability) {
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

std::optional<scalar_value> fmi2_to_start_value(fmi2_import_variable_t* fmiVariable, variable_type type)
{
    if (fmi2_import_get_variable_has_start(fmiVariable) == 0) {
        return std::nullopt;
    }
    switch (type) {
        case variable_type::real:
            return fmi2_import_get_real_variable_start(fmi2_import_get_variable_as_real(fmiVariable));
        case variable_type::integer:
            return fmi2_import_get_integer_variable_start(fmi2_import_get_variable_as_integer(fmiVariable));
        case variable_type::boolean:
            return fmi2_import_get_boolean_variable_start(fmi2_import_get_variable_as_boolean(fmiVariable)) != 0;
        case variable_type::string:
            return std::string(fmi2_import_get_string_variable_start(fmi2_import_get_variable_as_string(fmiVariable)));
        case variable_type::enumeration:
            return enumeration_value{
                fmi2_import_get_enum_variable_start(
                    fmi2_import_get_variable_as_enum(fmiVariable)),
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
    const auto causality = fmi2_import_get_causality(fmiVariable);
    const auto variability = fmi2_import_get_variability(fmiVariable);
    description.causality = to_variable_causality(causality);
    description.variability = to_variable_variability(variability);
    description.start = fmi2_to_start_value(fmiVariable, type);
    if (causality != fmi2_causality_enu_input &&
        causality != fmi2_causality_enu_independent) {
        auto initial = fmi2_import_get_initial(fmiVariable);
        if (initial == fmi2_initial_enu_unknown) {
            initial = fmi2_get_default_initial(variability, causality);
        }
        switch (initial) {
            case fmi2_initial_enu_exact:
                description.initial = variable_initial::exact;
                break;
            case fmi2_initial_enu_approx:
                description.initial = variable_initial::approximate;
                break;
            case fmi2_initial_enu_calculated:
                description.initial = variable_initial::calculated;
                break;
            case fmi2_initial_enu_unknown:
                break;
        }
    }
    if (auto* const previous = fmi2_import_get_previous(fmiVariable)) {
        description.previous = fmi2_import_get_variable_vr(previous);
    }
    if (causality == fmi2_causality_enu_input) {
        description.can_handle_multiple_set_per_time_instant =
            fmi2_import_get_canHandleMultipleSetPerTimeInstant(fmiVariable) != 0;
    }
    if (auto* const declaredType =
            fmi2_import_get_variable_declared_type(fmiVariable)) {
        description.declared_type =
            nonnull(fmi2_import_get_type_name(declaredType));
    }
    if (type == variable_type::real) {
        auto* const real = fmi2_import_get_variable_as_real(fmiVariable);
        description.quantity =
            optional_string(fmi2_import_get_real_variable_quantity(real));
        if (auto* const unit = fmi2_import_get_real_variable_unit(real)) {
            description.unit = nonnull(fmi2_import_get_unit_name(unit));
        }
        if (auto* const display = fmi2_import_get_real_variable_display_unit(real)) {
            description.display_unit =
                nonnull(fmi2_import_get_display_unit_name(display));
        }
        description.min = fmi2_import_get_real_variable_min(real);
        description.max = fmi2_import_get_real_variable_max(real);
        description.nominal = fmi2_import_get_real_variable_nominal(real);
        description.relative_quantity =
            fmi2_import_get_real_variable_relative_quantity(real) != 0;
        description.unbounded = fmi2_import_get_real_variable_unbounded(real) != 0;
    } else if (type == variable_type::integer) {
        auto* const integer = fmi2_import_get_variable_as_integer(fmiVariable);
        description.quantity =
            optional_string(fmi2_import_get_integer_variable_quantity(integer));
        description.min = fmi2_import_get_integer_variable_min(integer);
        description.max = fmi2_import_get_integer_variable_max(integer);
    } else if (type == variable_type::enumeration) {
        auto* const enumeration =
            fmi2_import_get_variable_as_enum(fmiVariable);
        description.quantity =
            optional_string(fmi2_import_get_enum_variable_quantity(enumeration));
    }
    return description;
}

namespace
{

void add_enumeration_type_metadata(
    variable_type_definition& definition,
    fmi2_import_enumeration_typedef_t* enumeration)
{
    if (enumeration == nullptr || definition.name.empty()) {
        throw error(
            make_error_code(errc::bad_file),
            "FMI 2.0 Enumeration declared type is incomplete");
    }

    const auto itemCount = fmi2_import_get_enum_type_size(enumeration);
    if (itemCount == 0) {
        throw error(
            make_error_code(errc::bad_file),
            "FMI 2.0 Enumeration declared type '" + definition.name +
                "' has no items");
    }

    std::unordered_set<std::string> itemNames;
    std::unordered_set<int> itemValues;
    definition.enumeration_items.reserve(itemCount);
    for (unsigned int item = 1; item <= itemCount; ++item) {
        const auto* const name = nonnull(
            fmi2_import_get_enum_type_item_name(enumeration, item));
        const auto value =
            fmi2_import_get_enum_type_item_value(enumeration, item);
        if (*name == '\0' || !itemNames.emplace(name).second ||
            !itemValues.emplace(value).second) {
            throw error(
                make_error_code(errc::bad_file),
                "FMI 2.0 Enumeration declared type '" +
                    definition.name +
                    "' contains duplicate or empty item metadata");
        }
        definition.enumeration_items.push_back(
            enumeration_item{
                name,
                static_cast<std::int64_t>(value),
                nonnull(fmi2_import_get_enum_type_item_description(
                    enumeration, item)),
            });
    }

    const auto minItem = boost::range::min_element(
        definition.enumeration_items,
        [](const auto& lhs, const auto& rhs) {
            return lhs.value < rhs.value;
        });
    const auto maxItem = boost::range::max_element(
        definition.enumeration_items,
        [](const auto& lhs, const auto& rhs) {
            return lhs.value < rhs.value;
        });
    definition.min = enumeration_value{minItem->value};
    definition.max = enumeration_value{maxItem->value};
}

} // namespace

std::vector<variable_type_definition>
to_type_definitions(fmi2_import_t* fmu)
{
    auto* const typeDefinitions = fmi2_import_get_type_definitions(fmu);
    if (typeDefinitions == nullptr) {
        throw error(
            make_error_code(errc::bad_file),
            "FMI 2.0 model has no readable TypeDefinitions element");
    }

    std::vector<variable_type_definition> result;
    const auto count =
        fmi2_import_get_type_definition_number(typeDefinitions);
    result.reserve(count);
    for (unsigned int i = 0; i < count; ++i) {
        auto* const typeDefinition =
            fmi2_import_get_typedef(typeDefinitions, i);
        if (typeDefinition == nullptr) {
            throw error(
                make_error_code(errc::bad_file),
                "FMI 2.0 model contains an invalid declared type definition");
        }

        variable_type_definition definition;
        definition.name =
            nonnull(fmi2_import_get_type_name(typeDefinition));
        definition.description =
            nonnull(fmi2_import_get_type_description(typeDefinition));
        definition.quantity =
            optional_string(fmi2_import_get_type_quantity(typeDefinition));
        definition.type =
            to_variable_type(fmi2_import_get_base_type(typeDefinition));

        if (definition.type == variable_type::enumeration) {
            add_enumeration_type_metadata(
                definition, fmi2_import_get_type_as_enum(typeDefinition));
        } else if (definition.type == variable_type::real) {
            if (auto* const real = fmi2_import_get_type_as_real(typeDefinition)) {
                if (auto* const unit = fmi2_import_get_real_type_unit(real)) {
                    definition.unit = nonnull(fmi2_import_get_unit_name(unit));
                }
                if (auto* const display = fmi2_import_get_type_display_unit(real)) {
                    definition.display_unit =
                        nonnull(fmi2_import_get_display_unit_name(display));
                }
                definition.min = fmi2_import_get_real_type_min(real);
                definition.max = fmi2_import_get_real_type_max(real);
                definition.nominal = fmi2_import_get_real_type_nominal(real);
                definition.relative_quantity =
                    fmi2_import_get_real_type_is_relative_quantity(real) != 0;
                definition.unbounded =
                    fmi2_import_get_real_type_is_unbounded(real) != 0;
            }
        } else if (definition.type == variable_type::integer) {
            auto* const integer = fmi2_import_get_type_as_int(typeDefinition);
            if (integer != nullptr) {
                definition.min = fmi2_import_get_integer_type_min(integer);
                definition.max = fmi2_import_get_integer_type_max(integer);
            }
        }

        result.push_back(std::move(definition));
    }
    return result;
}

std::vector<unit_definition> to_unit_definitions(fmi2_import_t* fmu)
{
    auto* const list = fmi2_import_get_unit_definitions(fmu);
    if (list == nullptr) {
        return {};
    }
    const auto count = fmi2_import_get_unit_definitions_number(list);
    std::vector<unit_definition> result;
    result.reserve(count);
    for (unsigned int i = 0; i < count; ++i) {
        auto* const unit = fmi2_import_get_unit(list, i);
        if (unit == nullptr) {
            throw error(make_error_code(errc::bad_file),
                "FMI 2.0 model contains an invalid unit definition");
        }
        const auto exponents = gsl::make_span(
            fmi2_import_get_SI_unit_exponents(unit), fmi2_SI_base_units_Num);
        unit_definition converted;
        converted.name = nonnull(fmi2_import_get_unit_name(unit));
        converted.kilogram = gsl::at(exponents, fmi2_SI_base_unit_kg);
        converted.metre = gsl::at(exponents, fmi2_SI_base_unit_m);
        converted.second = gsl::at(exponents, fmi2_SI_base_unit_s);
        converted.ampere = gsl::at(exponents, fmi2_SI_base_unit_A);
        converted.kelvin = gsl::at(exponents, fmi2_SI_base_unit_K);
        converted.mole = gsl::at(exponents, fmi2_SI_base_unit_mol);
        converted.candela = gsl::at(exponents, fmi2_SI_base_unit_cd);
        converted.radian = gsl::at(exponents, fmi2_SI_base_unit_rad);
        converted.factor = fmi2_import_get_SI_unit_factor(unit);
        converted.offset = fmi2_import_get_SI_unit_offset(unit);
        const auto displayCount = fmi2_import_get_unit_display_unit_number(unit);
        converted.display_units.reserve(displayCount);
        for (unsigned int j = 0; j < displayCount; ++j) {
            auto* const display = fmi2_import_get_unit_display_unit(unit, j);
            if (display == nullptr) {
                throw error(make_error_code(errc::bad_file),
                    "FMI 2.0 model contains an invalid display unit");
            }
            converted.display_units.push_back({
                nonnull(fmi2_import_get_display_unit_name(display)),
                fmi2_import_get_display_unit_factor(display),
                fmi2_import_get_display_unit_offset(display),
                false,
            });
        }
        result.push_back(std::move(converted));
    }
    return result;
}

} // namespace cosim::fmi
