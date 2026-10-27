/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/fmi/glue.hpp"

#include "cosim/error.hpp"
#include "cosim/exception.hpp"

#include <boost/lexical_cast.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <algorithm>
#include <cassert>
#include <unordered_map>
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


void read_xml_metadata(
    std::vector<unit_definition>& units,
    const filesystem::path& modelDescription,
    fmi2_enumeration_restrictions* enumerationRestrictions)
{
    boost::property_tree::ptree xml;
    boost::property_tree::read_xml(modelDescription.string(), xml);
    std::unordered_map<std::string, bool> definitions;
    if (const auto unitDefinitions =
            xml.get_child_optional("fmiModelDescription.UnitDefinitions")) {
        for (const auto& entry : *unitDefinitions) {
            if (entry.first != "Unit") continue;
            const auto name =
                entry.second.get_optional<std::string>("<xmlattr>.name");
            if (!name || !definitions.emplace(
                    *name, !!entry.second.get_child_optional("BaseUnit")).second) {
                throw error(make_error_code(errc::bad_file),
                    "Invalid or duplicate FMI unit definition name");
            }
        }
    }
    for (auto& unit : units) {
        const auto definition = definitions.find(unit.name);
        if (definition != definitions.end()) {
            unit.has_si_base_unit = definition->second;
        }
    }
    if (enumerationRestrictions == nullptr) return;
    if (const auto variables =
            xml.get_child_optional("fmiModelDescription.ModelVariables")) {
        for (const auto& entry : *variables) {
            if (entry.first != "ScalarVariable") continue;
            const auto enumeration = entry.second.get_child_optional("Enumeration");
            if (!enumeration) continue;
            const auto name =
                entry.second.get_optional<std::string>("<xmlattr>.name");
            if (!name) {
                throw error(make_error_code(errc::bad_file),
                    "FMI 2.0 Enumeration variable has no name");
            }
            fmi2_enumeration_restriction restriction;
            const auto parseBound = [&](const char* attribute) -> std::optional<int> {
                const auto value = enumeration->get_optional<std::string>(attribute);
                if (!value) return std::nullopt;
                try {
                    return boost::lexical_cast<int>(*value);
                } catch (const boost::bad_lexical_cast&) {
                    throw error(make_error_code(errc::bad_file),
                        "Invalid Enumeration bound for variable '" + *name + "'");
                }
            };
            restriction.min = parseBound("<xmlattr>.min");
            restriction.max = parseBound("<xmlattr>.max");
            if (!enumerationRestrictions->emplace(*name, restriction).second) {
                throw error(make_error_code(errc::bad_file),
                    "Duplicate FMI 2.0 Enumeration variable '" + *name + "'");
            }
        }
    }
}


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
    description.description =
        nonnull(fmi1_import_get_variable_description(fmiVariable));
    description.reference = fmi1_import_get_variable_vr(fmiVariable);
    description.type = type;
    description.causality = to_variable_causality(fmi1_import_get_causality(fmiVariable), fmiVariability);
    description.variability = to_variable_variability(fmiVariability);
    description.start = fmi1_to_start_value(fmiVariable, type);
    description.alias_kind =
        to_alias_kind(fmi1_import_get_variable_alias_kind(fmiVariable));
    if (const auto declaredType =
            fmi1_import_get_variable_declared_type(fmiVariable)) {
        description.declared_type = nonnull(fmi1_import_get_type_name(declaredType));
    }
    if (type == variable_type::real) {
        const auto real = fmi1_import_get_variable_as_real(fmiVariable);
        description.quantity =
            optional_string(fmi1_import_get_real_variable_quantity(real));
        if (const auto unit = fmi1_import_get_real_variable_unit(real)) {
            description.unit = nonnull(fmi1_import_get_unit_name(unit));
        }
        if (const auto display = fmi1_import_get_real_variable_display_unit(real)) {
            description.display_unit =
                nonnull(fmi1_import_get_display_unit_name(display));
        }
        description.min = fmi1_import_get_real_variable_min(real);
        description.max = fmi1_import_get_real_variable_max(real);
        description.nominal = fmi1_import_get_real_variable_nominal(real);
        description.relative_quantity =
            fmi1_import_get_real_variable_relative_quantity(real) != 0;
    } else if (type == variable_type::integer) {
        const auto integer = fmi1_import_get_variable_as_integer(fmiVariable);
        description.quantity =
            optional_string(fmi1_import_get_integer_variable_quantity(integer));
        description.min = fmi1_import_get_integer_variable_min(integer);
        description.max = fmi1_import_get_integer_variable_max(integer);
    } else if (type == variable_type::enumeration) {
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
        } else if (definition.type == variable_type::real) {
            if (const auto real = fmi1_import_get_type_as_real(typeDefinition)) {
                if (const auto unit = fmi1_import_get_real_type_unit(real)) {
                    definition.unit = nonnull(fmi1_import_get_unit_name(unit));
                }
                if (const auto display = fmi1_import_get_type_display_unit(real)) {
                    definition.display_unit =
                        nonnull(fmi1_import_get_display_unit_name(display));
                }
                definition.min = fmi1_import_get_real_type_min(real);
                definition.max = fmi1_import_get_real_type_max(real);
                definition.nominal = fmi1_import_get_real_type_nominal(real);
                definition.relative_quantity =
                    fmi1_import_get_real_type_is_relative_quantity(real) != 0;
            }
        } else if (definition.type == variable_type::integer) {
            if (const auto integer = fmi1_import_get_type_as_int(typeDefinition)) {
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
    const auto list = fmi1_import_get_unit_definitions(fmu);
    if (list == nullptr) return {};
    const auto count = fmi1_import_get_unit_definitions_number(list);
    std::vector<unit_definition> result;
    result.reserve(count);
    for (unsigned int i = 0; i < count; ++i) {
        const auto unit = fmi1_import_get_unit(list, i);
        if (unit == nullptr) {
            throw error(make_error_code(errc::bad_file),
                "FMI 1.0 model contains an invalid unit definition");
        }
        unit_definition converted;
        converted.name = nonnull(fmi1_import_get_unit_name(unit));
        const auto displayCount = fmi1_import_get_unit_display_unit_number(unit);
        converted.display_units.reserve(displayCount);
        for (unsigned int j = 0; j < displayCount; ++j) {
            const auto display = fmi1_import_get_unit_display_unit(unit, j);
            if (display == nullptr) {
                throw error(make_error_code(errc::bad_file),
                    "FMI 1.0 model contains an invalid display unit");
            }
            converted.display_units.push_back({
                nonnull(fmi1_import_get_display_unit_name(display)),
                fmi1_import_get_display_unit_gain(display),
                fmi1_import_get_display_unit_offset(display),
                false});
        }
        result.push_back(std::move(converted));
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
    if (const auto previous = fmi2_import_get_previous(fmiVariable)) {
        description.previous = fmi2_import_get_variable_vr(previous);
    }
    if (causality == fmi2_causality_enu_input) {
        description.can_handle_multiple_set_per_time_instant =
            fmi2_import_get_canHandleMultipleSetPerTimeInstant(fmiVariable) != 0;
    }
    if (const auto declaredType =
            fmi2_import_get_variable_declared_type(fmiVariable)) {
        description.declared_type =
            nonnull(fmi2_import_get_type_name(declaredType));
    }
    if (type == variable_type::real) {
        const auto real = fmi2_import_get_variable_as_real(fmiVariable);
        description.quantity =
            optional_string(fmi2_import_get_real_variable_quantity(real));
        if (const auto unit = fmi2_import_get_real_variable_unit(real)) {
            description.unit = nonnull(fmi2_import_get_unit_name(unit));
        }
        if (const auto display = fmi2_import_get_real_variable_display_unit(real)) {
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
        const auto integer = fmi2_import_get_variable_as_integer(fmiVariable);
        description.quantity =
            optional_string(fmi2_import_get_integer_variable_quantity(integer));
        description.min = fmi2_import_get_integer_variable_min(integer);
        description.max = fmi2_import_get_integer_variable_max(integer);
    } else if (type == variable_type::enumeration) {
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
        throw error(
            make_error_code(errc::bad_file),
            "FMI 2.0 model has no readable TypeDefinitions element");
    }

    std::vector<variable_type_definition> result;
    const auto count =
        fmi2_import_get_type_definition_number(typeDefinitions);
    result.reserve(count);
    for (unsigned int i = 0; i < count; ++i) {
        const auto typeDefinition =
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
            const auto enumeration =
                fmi2_import_get_type_as_enum(typeDefinition);
            if (enumeration == nullptr || definition.name.empty()) {
                throw error(
                    make_error_code(errc::bad_file),
                    "FMI 2.0 Enumeration declared type is incomplete");
            }

            const auto itemCount =
                fmi2_import_get_enum_type_size(enumeration);
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
                const auto name = nonnull(
                    fmi2_import_get_enum_type_item_name(enumeration, item));
                const auto value =
                    fmi2_import_get_enum_type_item_value(enumeration, item);
                if (name[0] == '\0' || !itemNames.emplace(name).second ||
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
                            enumeration, item))});
            }

            const auto minItem = std::min_element(
                definition.enumeration_items.begin(),
                definition.enumeration_items.end(),
                [](const auto& lhs, const auto& rhs) {
                    return lhs.value < rhs.value;
                });
            const auto maxItem = std::max_element(
                definition.enumeration_items.begin(),
                definition.enumeration_items.end(),
                [](const auto& lhs, const auto& rhs) {
                    return lhs.value < rhs.value;
                });
            definition.min = enumeration_value{minItem->value};
            definition.max = enumeration_value{maxItem->value};
        } else if (definition.type == variable_type::real) {
            if (const auto real = fmi2_import_get_type_as_real(typeDefinition)) {
                if (const auto unit = fmi2_import_get_real_type_unit(real)) {
                    definition.unit = nonnull(fmi2_import_get_unit_name(unit));
                }
                if (const auto display = fmi2_import_get_type_display_unit(real)) {
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
            const auto integer = fmi2_import_get_type_as_int(typeDefinition);
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
    const auto list = fmi2_import_get_unit_definitions(fmu);
    if (list == nullptr) return {};
    const auto count = fmi2_import_get_unit_definitions_number(list);
    std::vector<unit_definition> result;
    result.reserve(count);
    for (unsigned int i = 0; i < count; ++i) {
        const auto unit = fmi2_import_get_unit(list, i);
        if (unit == nullptr) {
            throw error(make_error_code(errc::bad_file),
                "FMI 2.0 model contains an invalid unit definition");
        }
        const auto exponents = fmi2_import_get_SI_unit_exponents(unit);
        unit_definition converted;
        converted.name = nonnull(fmi2_import_get_unit_name(unit));
        converted.kilogram = exponents[fmi2_SI_base_unit_kg];
        converted.metre = exponents[fmi2_SI_base_unit_m];
        converted.second = exponents[fmi2_SI_base_unit_s];
        converted.ampere = exponents[fmi2_SI_base_unit_A];
        converted.kelvin = exponents[fmi2_SI_base_unit_K];
        converted.mole = exponents[fmi2_SI_base_unit_mol];
        converted.candela = exponents[fmi2_SI_base_unit_cd];
        converted.radian = exponents[fmi2_SI_base_unit_rad];
        converted.factor = fmi2_import_get_SI_unit_factor(unit);
        converted.offset = fmi2_import_get_SI_unit_offset(unit);
        const auto displayCount = fmi2_import_get_unit_display_unit_number(unit);
        converted.display_units.reserve(displayCount);
        for (unsigned int j = 0; j < displayCount; ++j) {
            const auto display = fmi2_import_get_unit_display_unit(unit, j);
            if (display == nullptr) {
                throw error(make_error_code(errc::bad_file),
                    "FMI 2.0 model contains an invalid display unit");
            }
            converted.display_units.push_back({
                nonnull(fmi2_import_get_display_unit_name(display)),
                fmi2_import_get_display_unit_factor(display),
                fmi2_import_get_display_unit_offset(display),
                false});
        }
        result.push_back(std::move(converted));
    }
    return result;
}


} // namespace fmi
} // namespace cosim
