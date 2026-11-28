/**
 *  \file
 *  Conversions between FMI variable attributes and "our" attributes.
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_FMI_GLUE_HPP
#define COSIM_FMI_GLUE_HPP

#include "cosim/fmi/fmilib.h" // IWYU pragma: keep
#include "cosim/model_description.hpp"
#include <cosim/fs_portability.hpp>

#include <FMI/fmi_import_context.h>
#include <FMI1/fmi1_enums.h>
#include <FMI1/fmi1_import_variable.h>
#include <FMI2/fmi2_enums.h>
#include <FMI2/fmi2_import_variable.h>
#include <FMI3/fmi3_enums.h>
#include <FMI3/fmi3_import_variable.h>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>


namespace cosim::fmi
{


/// Converts an FMI 1.0 base type to "our" data type.
variable_type to_variable_type(fmi1_base_type_enu_t type);


/// Converts an FMI 2.0 base type to "our" data type.
variable_type to_variable_type(fmi2_base_type_enu_t type);


/// Converts an FMI 3.0 base type to "our" data type.
variable_type to_variable_type(fmi3_base_type_enu_t type);


/**
 *  Converts an FMI 1.0 variable causality to "our" corresponding causality.
 *
 *  The causality mapping is not unique, so the variable's variability is also
 *  needed.
 */
variable_causality to_variable_causality(
    fmi1_causality_enu_t causality,
    fmi1_variability_enu_t variability);


/// Converts an FMI 2.0 variable causality to "our" corresponding causality.
variable_causality to_variable_causality(fmi2_causality_enu_t causality);


/// Converts an FMI 3.0 variable causality to "our" corresponding causality.
variable_causality to_variable_causality(fmi3_causality_enu_t causality);


/// Converts an FMI 1.0 variable variability to "our" corresponding variability.
variable_variability to_variable_variability(fmi1_variability_enu_t variability);


/// Converts an FMI 2.0 variable variability to "our" corresponding variability.
variable_variability to_variable_variability(fmi2_variability_enu_t variability);


/// Converts an FMI 3.0 variable variability to "our" corresponding variability.
variable_variability to_variable_variability(fmi3_variability_enu_t variability);


/// Converts an FMI 1.0 variable description to a `variable_description` object.
variable_description to_variable_description(fmi1_import_variable_t* fmiVariable);

/// Converts all FMI 1.0 declared type definitions.
std::vector<variable_type_definition>
to_type_definitions(fmi1_import_t* fmu);

/// Converts all FMI 1.0 unit definitions.
std::vector<unit_definition> to_unit_definitions(fmi1_import_t* fmu);

/// Converts an FMI 2.0 variable description to a `variable_description` object.
variable_description to_variable_description(fmi2_import_variable_t* fmiVariable);


/// Converts all FMI 2.0 declared type definitions.
std::vector<variable_type_definition>
to_type_definitions(fmi2_import_t* fmu);

/// Converts all FMI 2.0 unit definitions.
std::vector<unit_definition> to_unit_definitions(fmi2_import_t* fmu);

struct fmi2_enumeration_restriction
{
    std::optional<int> min;
    std::optional<int> max;
};

using fmi2_enumeration_restrictions =
    std::unordered_map<std::string, fmi2_enumeration_restriction>;

using fmi3_array_start_counts = std::unordered_map<std::string, std::size_t>;

/// Best-effort loads XML-only unit presence and, for FMI 2, Enumeration restrictions.
///
/// Malformed optional metadata is omitted with a warning so an FMU accepted by
/// FMI Library remains loadable.
void read_xml_metadata(
    std::vector<unit_definition>& units,
    const filesystem::path& modelDescription,
    fmi2_enumeration_restrictions* enumerationRestrictions = nullptr,
    fmi3_array_start_counts* arrayStartCounts = nullptr);

/// Imports output dependencies, preserving absent versus explicitly empty lists.
void read_fmi1_output_dependencies(
    model_description& description, const filesystem::path& modelDescription);
void read_output_dependencies(
    model_description& description, fmi2_import_t* fmu,
    const filesystem::path& modelDescription);
/// Imports the FMI 3 ModelStructure, including the output dependencies.
void read_model_structure(
    model_description& description, fmi3_import_t* fmu,
    const filesystem::path& modelDescription);

/**
 *  Converts an FMI 3.0 variable description to a `variable_description`.
 *
 *  `structuralParameterStarts` is used to resolve variable-size array starts.
 */
variable_description to_variable_description(
    fmi3_import_t* fmu,
    fmi3_import_variable_t* fmiVariable,
    const std::unordered_map<value_reference, std::uint64_t>&
        structuralParameterStarts,
    const fmi3_array_start_counts& arrayStartCounts);


/// Converts all FMI 3.0 declared type definitions.
std::vector<variable_type_definition>
to_type_definitions(fmi3_import_t* fmu);


/// Converts all FMI 3.0 unit definitions.
std::vector<unit_definition> to_unit_definitions(fmi3_import_t* fmu);


} // namespace cosim::fmi

#endif // header guard
