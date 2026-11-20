/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/fmi/glue.hpp"

#include "glue_internal.hpp"

#include "cosim/exception.hpp"
#include "cosim/log/logger.hpp"
#include "cosim/model_description.hpp"

#include <boost/lexical_cast.hpp>
#include <boost/lexical_cast/bad_lexical_cast.hpp>
#include <boost/log/sources/severity_feature.hpp>
#include <boost/property_tree/exceptions.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ptree_fwd.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/range/algorithm/count_if.hpp>
#include <gsl/narrow>
#include <gsl/span>
#include <gsl/span_ext>
#include <gsl/util>

#include <FMI/fmi_import_context.h>
#include <FMI3/fmi3_enums.h>
#include <FMI3/fmi3_import.h>
#include <FMI3/fmi3_import_variable.h>
#include <FMI3/fmi3_import_variable_list.h>
#include <cassert>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>


namespace cosim::fmi
{

using glue_detail::nonnull;

namespace
{
void read_unit_definitions(
    std::vector<unit_definition>& units,
    const boost::property_tree::ptree& xml,
    const filesystem::path& modelDescription)
{
    std::unordered_map<std::string, bool> definitions;
    if (const auto unitDefinitions =
            xml.get_child_optional("fmiModelDescription.UnitDefinitions")) {
        for (const auto& entry : *unitDefinitions) {
            if (entry.first != "Unit") {
                continue;
            }
            const auto name =
                entry.second.get_optional<std::string>("<xmlattr>.name");
            if (!name || name->empty()) {
                BOOST_LOG_SEV(log::logger(), log::warning)
                    << "Skipping FMI unit definition without a name in '"
                    << modelDescription.string() << "'";
                continue;
            }
            if (!definitions.emplace(
                                *name, !!entry.second.get_child_optional("BaseUnit"))
                    .second) {
                BOOST_LOG_SEV(log::logger(), log::warning)
                    << "Skipping duplicate FMI unit definition '" << *name
                    << "' in '" << modelDescription.string() << "'";
            }
        }
    }
    for (auto& unit : units) {
        const auto definition = definitions.find(unit.name);
        if (definition != definitions.end()) {
            unit.has_si_base_unit = definition->second;
        }
    }
}

void read_array_start_counts(
    const boost::property_tree::ptree& xml, fmi3_array_start_counts& arrayStartCounts)
{
    if (const auto variables = xml.get_child_optional("fmiModelDescription.ModelVariables")) {
        for (const auto& entry : *variables) {
            if (!entry.second.get_child_optional("Dimension")) {
                continue;
            }
            const auto name = entry.second.get_optional<std::string>("<xmlattr>.name");
            if (!name) {
                continue;
            }
            std::size_t count = 0;
            if (const auto start = entry.second.get_optional<std::string>("<xmlattr>.start")) {
                bool inToken = false;
                for (const auto character : *start) {
                    const bool whitespace = character == ' ' || character == '\t' ||
                        character == '\r' || character == '\n';
                    if (!whitespace && !inToken) {
                        ++count;
                    }
                    inToken = !whitespace;
                }
            } else {
                count = boost::range::count_if(entry.second,
                    [](const auto& child) { return child.first == "Start"; });
            }
            arrayStartCounts.emplace(*name, count);
        }
    }
}

std::optional<int> read_enumeration_bound(
    const boost::property_tree::ptree& enumeration,
    const char* attribute,
    const std::string& name,
    const filesystem::path& modelDescription,
    bool& valid)
{
    const auto value = enumeration.get_optional<std::string>(attribute);
    if (!value) {
        return std::nullopt;
    }
    try {
        return boost::lexical_cast<int>(*value);
    } catch (const boost::bad_lexical_cast&) {
        BOOST_LOG_SEV(log::logger(), log::warning)
            << "Skipping FMI 2.0 Enumeration restriction for variable '"
            << name << "' in '" << modelDescription.string()
            << "': invalid " << attribute << " value '" << *value << "'";
        valid = false;
        return std::nullopt;
    }
}

void read_enumeration_restrictions(
    const boost::property_tree::ptree& xml,
    fmi2_enumeration_restrictions& enumerationRestrictions,
    const filesystem::path& modelDescription)
{
    const auto variables = xml.get_child_optional("fmiModelDescription.ModelVariables");
    if (!variables) {
        return;
    }
    for (const auto& entry : *variables) {
        if (entry.first != "ScalarVariable") {
            continue;
        }
        const auto enumeration = entry.second.get_child_optional("Enumeration");
        if (!enumeration) {
            continue;
        }
        const auto name =
            entry.second.get_optional<std::string>("<xmlattr>.name");
        if (!name || name->empty()) {
            BOOST_LOG_SEV(log::logger(), log::warning)
                << "Skipping FMI 2.0 Enumeration restriction without a variable name in '"
                << modelDescription.string() << "'";
            continue;
        }
        fmi2_enumeration_restriction restriction;
        bool valid = true;
        restriction.min = read_enumeration_bound(
            *enumeration, "<xmlattr>.min", *name, modelDescription, valid);
        restriction.max = read_enumeration_bound(
            *enumeration, "<xmlattr>.max", *name, modelDescription, valid);
        if (!valid) {
            continue;
        }
        if (!enumerationRestrictions.emplace(*name, restriction).second) {
            BOOST_LOG_SEV(log::logger(), log::warning)
                << "Skipping duplicate FMI 2.0 Enumeration restriction for variable '"
                << *name << "' in '" << modelDescription.string() << "'";
        }
    }
}
} // namespace

void read_xml_metadata(
    std::vector<unit_definition>& units,
    const filesystem::path& modelDescription,
    fmi2_enumeration_restrictions* enumerationRestrictions,
    fmi3_array_start_counts* arrayStartCounts)
{
    boost::property_tree::ptree xml;
    try {
        boost::property_tree::read_xml(modelDescription.string(), xml);
    } catch (const boost::property_tree::ptree_error& exception) {
        if (arrayStartCounts != nullptr) {
            throw error(make_error_code(errc::bad_file),
                "Cannot verify FMI 3 array starts in " + modelDescription.string() +
                    ": " + exception.what());
        }
        BOOST_LOG_SEV(log::logger(), log::warning)
            << "Cannot read optional FMI XML metadata from '"
            << modelDescription.string() << "': " << exception.what();
        return;
    }
    read_unit_definitions(units, xml, modelDescription);
    if (arrayStartCounts != nullptr) {
        read_array_start_counts(xml, *arrayStartCounts);
    }
    if (enumerationRestrictions != nullptr) {
        read_enumeration_restrictions(xml, *enumerationRestrictions, modelDescription);
    }
}

namespace
{
void validate_dependency_entry(
    const boost::property_tree::ptree& entry,
    const std::unordered_set<value_reference>& references)
{
    const auto dependencyText =
        entry.get_optional<std::string>("<xmlattr>.dependencies");
    const auto kindText =
        entry.get_optional<std::string>("<xmlattr>.dependenciesKind");
    std::size_t count = 0;
    if (dependencyText) {
        std::istringstream input(*dependencyText);
        std::string token;
        while (input >> token) {
            std::uint64_t number = 0;
            const auto* begin = token.data();
            if (token.front() == '+') {
                begin = std::next(begin);
            }
            const auto* end = std::next(token.data(), gsl::narrow<gsl::index>(token.size()));
            const auto result = std::from_chars(begin, end, number);
            if (result.ec != std::errc{} || result.ptr != end ||
                // NOLINTNEXTLINE(readability-redundant-parentheses): Windows may define max as a macro.
                number > (std::numeric_limits<value_reference>::max)() ||
                references.count(static_cast<value_reference>(number)) == 0) {
                throw error(make_error_code(errc::bad_file),
                    "Invalid FMI 3 ModelStructure dependency '" + token + "'");
            }
            ++count;
        }
    }
    if (kindText) {
        std::istringstream input(*kindText);
        std::string kind;
        std::size_t kindCount = 0;
        while (input >> kind) {
            if (kind != "dependent" && kind != "constant" && kind != "fixed" &&
                kind != "tunable" && kind != "discrete") {
                throw error(make_error_code(errc::bad_file),
                    "Invalid FMI ModelStructure dependency kind '" + kind + "'");
            }
            ++kindCount;
        }
        if (!dependencyText || kindCount != count) {
            throw error(make_error_code(errc::bad_file),
                "FMI ModelStructure dependencies and dependency kinds have different counts");
        }
    }
}

void validate_model_structure_dependencies(
    const filesystem::path& path, const model_description& description)
{
    boost::property_tree::ptree xml;
    try {
        boost::property_tree::read_xml(path.string(), xml);
    } catch (const boost::property_tree::ptree_error& exception) {
        throw error(make_error_code(errc::bad_file),
            "Could not read ModelStructure dependencies: " + std::string(exception.what()));
    }
    const auto structure = xml.get_child_optional("fmiModelDescription.ModelStructure");
    if (!structure) {
        return;
    }
    std::unordered_set<value_reference> references;
    for (const auto& variable : description.variables) {
        references.insert(variable.reference);
    }
    for (const auto& entry : *structure) {
        const auto& element = entry.first;
        if (element == "Output" || element == "ContinuousStateDerivative" ||
            element == "ClockedState" || element == "InitialUnknown" ||
            element == "EventIndicator") {
            validate_dependency_entry(entry.second, references);
        }
    }
}

dependency_kind convert_dependency_kind(int kind)
{
    static_assert(fmi3_dependencies_kind_dependent == 0 &&
        fmi3_dependencies_kind_constant == 1 &&
        fmi3_dependencies_kind_fixed == 2 &&
        fmi3_dependencies_kind_tunable == 3 &&
        fmi3_dependencies_kind_discrete == 4);
    switch (kind) {
        case 0: return dependency_kind::dependent;
        case 1: return dependency_kind::constant;
        case 2: return dependency_kind::fixed;
        case 3: return dependency_kind::tunable;
        case 4: return dependency_kind::discrete;
        default:
            throw error(make_error_code(errc::bad_file),
                "Invalid FMI ModelStructure dependency kind");
    }
}

std::unordered_map<value_reference, variable_description*> fmi3_variable_index(
    model_description& description)
{
    std::unordered_map<value_reference, variable_description*> byReference;
    byReference.reserve(description.variables.size());
    for (auto& variable : description.variables) {
        if (!byReference.emplace(variable.reference, &variable).second) {
            throw error(make_error_code(errc::bad_file),
                "Duplicate FMI 3 value reference in ModelStructure");
        }
    }
    return byReference;
}

std::vector<variable_dependency> fmi3_dependencies(
    const std::string& name,
    const std::unordered_map<value_reference, variable_description*>& byReference,
    std::size_t* references,
    char* kinds,
    std::size_t count)
{
    std::vector<variable_dependency> dependencies;
    dependencies.reserve(count);
    const auto dependencyReferences = gsl::make_span(references, count);
    const auto dependencyKinds = gsl::make_span(kinds, count);
    auto kind = dependencyKinds.begin();
    for (const auto reference : dependencyReferences) {
        // NOLINTNEXTLINE(readability-redundant-parentheses): Windows may define max as a macro.
        if (reference > (std::numeric_limits<value_reference>::max)()) {
            throw error(make_error_code(errc::bad_file),
                "Invalid FMI 3 dependency reference for '" + name + "'");
        }
        const auto found =
            byReference.find(static_cast<value_reference>(reference));
        if (found == byReference.end()) {
            throw error(make_error_code(errc::bad_file),
                "Unknown FMI 3 dependency reference for '" + name + "'");
        }
        dependencies.push_back({
            {found->second->type, found->second->reference},
            convert_dependency_kind(*kind),
        });
        ++kind;
    }
    return dependencies;
}
} // namespace

void read_model_structure(
    model_description& description, fmi3_import_t* fmu,
    const filesystem::path& modelDescription)
{
    validate_model_structure_dependencies(modelDescription, description);
    const auto byReference = fmi3_variable_index(description);

    using dependency_getter = int (*)(
        fmi3_import_t*, fmi3_import_variable_t*, std::size_t*, int*, std::size_t**, char**);
    const auto readUnknowns = [&](fmi3_import_variable_list_t* list,
                                  dependency_getter getDependencies,
                                  bool outputsOnly) {
        std::vector<model_structure_unknown> unknowns;
        if (list == nullptr) {
            return unknowns;
        }
        const auto freeList = gsl::finally([&] { fmi3_import_free_variable_list(list); });
        for (std::size_t i = 0; i < fmi3_import_get_variable_list_size(list); ++i) {
            auto* unknown = fmi3_import_get_variable(list, i);
            if (unknown == nullptr) {
                throw error(make_error_code(errc::bad_file),
                    "FMI 3 ModelStructure contains an invalid unknown");
            }
            const std::string name = nonnull(fmi3_import_get_variable_name(unknown));
            const auto foundVariable = byReference.find(fmi3_import_get_variable_vr(unknown));
            if (foundVariable == byReference.end() ||
                (outputsOnly && foundVariable->second->causality != variable_causality::output)) {
                throw error(make_error_code(errc::bad_file),
                    "FMI 3 ModelStructure references an invalid variable '" + name + "'");
            }
            const auto& variable = *foundVariable->second;
            std::size_t count = 0;
            int dependsOnAll = 0;
            std::size_t* references = nullptr;
            char* kinds = nullptr;
            if (getDependencies(fmu, unknown, &count, &dependsOnAll, &references, &kinds) != 0 ||
                (count != 0 && (references == nullptr || kinds == nullptr))) {
                throw error(make_error_code(errc::bad_file),
                    "Could not read FMI 3 dependencies for '" + name + "'");
            }
            model_structure_unknown entry;
            entry.variable = {variable.type, variable.reference};
            if (count != 0 || !dependsOnAll) {
                entry.dependencies = fmi3_dependencies(
                    name, byReference, references, kinds, count);
            }
            unknowns.push_back(std::move(entry));
        }
        return unknowns;
    };

    auto& structure = description.structure;
    structure.outputs = readUnknowns(
        fmi3_import_get_outputs_list(fmu), &fmi3_import_get_output_dependencies, true);
    structure.continuous_state_derivatives = readUnknowns(
        fmi3_import_get_continuous_state_derivatives_list(fmu),
        &fmi3_import_get_continuous_state_derivative_dependencies, false);
    structure.clocked_states = readUnknowns(
        fmi3_import_get_clocked_states_list(fmu),
        &fmi3_import_get_clocked_state_dependencies, false);
    structure.initial_unknowns = readUnknowns(
        fmi3_import_get_initial_unknowns_list(fmu),
        &fmi3_import_get_initial_unknown_dependencies, false);
    structure.event_indicators = readUnknowns(
        fmi3_import_get_event_indicators_list(fmu),
        &fmi3_import_get_event_indicator_dependencies, false);

    for (const auto& output : structure.outputs) {
        if (!output.dependencies) {
            continue;
        }
        byReference.at(output.variable.reference)->output_dependencies = output.dependencies;
    }
}

} // namespace cosim::fmi
