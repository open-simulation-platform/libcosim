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
#include <boost/range/algorithm/find_if.hpp>
#include <gsl/narrow>
#include <gsl/span>
#include <gsl/span_ext>
#include <gsl/util>

#include <FMI/fmi_import_context.h>
#include <FMI2/fmi2_enums.h>
#include <FMI2/fmi2_import.h>
#include <FMI2/fmi2_import_variable.h>
#include <FMI2/fmi2_import_variable_list.h>
#include <FMI3/fmi3_enums.h>
#include <FMI3/fmi3_import.h>
#include <FMI3/fmi3_import_variable.h>
#include <FMI3/fmi3_import_variable_list.h>
#include <algorithm>
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
    bool fmi3,
    const std::unordered_set<value_reference>& references,
    std::size_t fmi2VariableCount)
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
                (fmi3 && (number > (std::numeric_limits<value_reference>::max)() || references.count(static_cast<value_reference>(number)) == 0)) ||
                (!fmi3 && (number == 0 || number > fmi2VariableCount))) {
                throw error(make_error_code(errc::bad_file),
                    "Invalid FMI ModelStructure dependency '" + token + "'");
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

std::vector<std::string> fmi2_variable_names(const boost::property_tree::ptree& xml)
{
    const auto variables = xml.get_child_optional("fmiModelDescription.ModelVariables");
    if (!variables) {
        throw error(make_error_code(errc::bad_file),
            "FMI 2 ModelVariables are missing");
    }
    std::vector<std::string> names;
    for (const auto& entry : *variables) {
        if (entry.first != "ScalarVariable") {
            continue;
        }
        const auto name = entry.second.get_optional<std::string>("<xmlattr>.name");
        if (!name) {
            throw error(make_error_code(errc::bad_file),
                "FMI 2 ScalarVariable has no name");
        }
        names.push_back(*name);
    }
    return names;
}

bool validate_output_dependencies_xml(
    const filesystem::path& path, const model_description& description, bool fmi3)
{
    boost::property_tree::ptree xml;
    try {
        boost::property_tree::read_xml(path.string(), xml);
    } catch (const boost::property_tree::ptree_error& exception) {
        throw error(make_error_code(errc::bad_file),
            "Could not read ModelStructure dependencies: " + std::string(exception.what()));
    }
    std::vector<std::string> fmi2Variables;
    if (!fmi3) {
        fmi2Variables = fmi2_variable_names(xml);
    }
    const auto outputs = xml.get_child_optional(fmi3
            ? "fmiModelDescription.ModelStructure"
            : "fmiModelDescription.ModelStructure.Outputs");
    std::unordered_set<value_reference> references;
    if (fmi3) {
        for (const auto& variable : description.variables) {
            references.insert(variable.reference);
        }
    }
    const auto isUnknown = [fmi3](const std::string& element) {
        return fmi3
            ? (element == "Output" || element == "ContinuousStateDerivative" ||
                  element == "ClockedState" || element == "InitialUnknown" ||
                  element == "EventIndicator")
            : element == "Unknown";
    };
    if (outputs) {
        for (const auto& entry : *outputs) {
            if (!isUnknown(entry.first)) {
                continue;
            }
            validate_dependency_entry(entry.second, fmi3, references, fmi2Variables.size());
        }
    }
    if (fmi3) {
        return true;
    }
    if (fmi2Variables.size() != description.variables.size() ||
        !std::equal(fmi2Variables.begin(), fmi2Variables.end(),
            description.variables.begin(), [](const auto& name, const auto& variable) {
                return name == variable.name;
            })) {
        BOOST_LOG_SEV(log::logger(), log::warning)
            << "FMI 2 imported variable order differs from ModelVariables in '"
            << path.string() << "'; treating output dependencies as unknown";
        return false;
    }
    return true;
}

dependency_kind convert_dependency_kind(int kind)
{
    static_assert(fmi2_dependency_factor_kind_dependent == 0 &&
        fmi2_dependency_factor_kind_constant == 1 &&
        fmi2_dependency_factor_kind_fixed == 2 &&
        fmi2_dependency_factor_kind_tunable == 3 &&
        fmi2_dependency_factor_kind_discrete == 4);
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

variable_description& output_by_name(
    model_description& description, const std::string& name)
{
    const auto entry = boost::range::find_if(
        description.variables,
        [&name](const auto& variable) { return variable.name == name; });
    if (entry == description.variables.end() ||
        entry->causality != variable_causality::output) {
        throw error(make_error_code(errc::bad_file),
            "FMI ModelStructure references an unknown output '" + name + "'");
    }
    return *entry;
}

std::optional<std::vector<variable_dependency>> fmi1_dependencies(
    const boost::property_tree::ptree& dependencies,
    const std::string& outputName,
    const std::unordered_map<std::string, const variable_description*>& imported,
    const std::unordered_map<std::string, std::string>& declared)
{
    std::vector<variable_dependency> result;
    for (const auto& dependency : dependencies) {
        if (dependency.first != "Name") {
            continue;
        }
        const auto dependencyName = dependency.second.get_value<std::string>();
        const auto declaration = declared.find(dependencyName);
        if (declaration == declared.end() || declaration->second != "input") {
            throw error(make_error_code(errc::bad_file),
                "Invalid FMI 1 direct dependency '" + dependencyName + "'");
        }
        const auto found = imported.find(dependencyName);
        if (found == imported.end()) {
            BOOST_LOG_SEV(log::logger(), log::warning)
                << "FMI 1 output '" << outputName << "' depends on omitted variable '"
                << dependencyName << "'; treating its dependencies as unknown";
            return std::nullopt;
        }
        result.push_back({
            {found->second->type, found->second->reference},
            dependency_kind::dependent,
        });
    }
    return result;
}

} // namespace

void read_fmi1_output_dependencies(
    model_description& description, const filesystem::path& modelDescription)
{
    boost::property_tree::ptree xml;
    try {
        boost::property_tree::read_xml(modelDescription.string(), xml);
    } catch (const boost::property_tree::ptree_error& exception) {
        throw error(make_error_code(errc::bad_file),
            "Could not read FMI 1 direct dependencies: " + std::string(exception.what()));
    }
    const auto variables = xml.get_child_optional("fmiModelDescription.ModelVariables");
    if (!variables) {
        return;
    }
    std::unordered_map<std::string, const variable_description*> imported;
    std::unordered_map<std::string, std::string> declared;
    for (const auto& variable : description.variables) {
        imported.emplace(variable.name, &variable);
    }
    for (const auto& entry : *variables) {
        if (entry.first != "ScalarVariable") {
            continue;
        }
        const auto name = entry.second.get<std::string>("<xmlattr>.name", "");
        declared.emplace(name, entry.second.get<std::string>("<xmlattr>.causality", "internal"));
    }
    for (const auto& entry : *variables) {
        if (entry.first != "ScalarVariable") {
            continue;
        }
        const auto dependencies = entry.second.get_child_optional("DirectDependency");
        if (!dependencies) {
            continue;
        }
        const auto name = entry.second.get<std::string>("<xmlattr>.name", "");
        if (entry.second.get<std::string>("<xmlattr>.causality", "internal") != "output") {
            throw error(make_error_code(errc::bad_file),
                "FMI 1 direct dependencies are only valid for an output");
        }
        const auto importedOutput = imported.find(name);
        if (importedOutput == imported.end()) {
            continue;
        }
        auto result = fmi1_dependencies(*dependencies, name, imported, declared);
        if (result) {
            output_by_name(description, name).output_dependencies = std::move(*result);
        }
    }
}

void read_output_dependencies(
    model_description& description, fmi2_import_t* fmu,
    const filesystem::path& modelDescription)
{
    if (!validate_output_dependencies_xml(modelDescription, description, false)) {
        return;
    }
    auto* outputs = fmi2_import_get_outputs_list(fmu);
    if (outputs == nullptr) {
        return;
    }
    const auto freeOutputs = gsl::finally([&] { fmi2_import_free_variable_list(outputs); });
    std::size_t* starts = nullptr;
    std::size_t* indices = nullptr;
    char* kinds = nullptr;
    fmi2_import_get_outputs_dependencies(fmu, &starts, &indices, &kinds);
    if (starts == nullptr) {
        return;
    }

    const auto outputCount = fmi2_import_get_variable_list_size(outputs);
    // NOLINTNEXTLINE(readability-redundant-parentheses): Windows may define max as a macro.
    if (outputCount == (std::numeric_limits<std::size_t>::max)()) {
        throw error(make_error_code(errc::bad_file),
            "FMI 2 output dependency list is too large");
    }
    const auto offsets = gsl::make_span(starts, outputCount + 1);
    for (std::size_t i = 0; i < outputCount; ++i) {
        auto* const output = fmi2_import_get_variable(outputs, i);
        if (output == nullptr) {
            throw error(make_error_code(errc::bad_file),
                "FMI 2 ModelStructure contains an invalid output");
        }
        auto& variable = output_by_name(
            description, nonnull(fmi2_import_get_variable_name(output)));
        const auto begin = gsl::at(offsets, gsl::narrow<gsl::index>(i));
        const auto end = gsl::at(offsets, gsl::narrow<gsl::index>(i + 1));
        if (end < begin || (end > begin && (indices == nullptr || kinds == nullptr))) {
            throw error(make_error_code(errc::bad_file),
                "Invalid FMI 2 output dependency list for '" + variable.name + "'");
        }
        if (end == begin) {
            variable.output_dependencies = std::vector<variable_dependency>{};
            continue;
        }
        const auto dependencyIndices =
            gsl::make_span(indices, end).subspan(begin, end - begin);
        const auto dependencyKinds =
            gsl::make_span(kinds, end).subspan(begin, end - begin);
        if (dependencyIndices.size() == 1 && dependencyIndices.front() == 0) {
            continue;
        }
        std::vector<variable_dependency> dependencies;
        dependencies.reserve(end - begin);
        auto kind = dependencyKinds.begin();
        for (const auto xmlIndex : dependencyIndices) {
            if (xmlIndex == 0 || xmlIndex > description.variables.size()) {
                throw error(make_error_code(errc::bad_file),
                    "Invalid FMI 2 dependency index for '" + variable.name + "'");
            }
            const auto& dependency = description.variables.at(xmlIndex - 1);
            dependencies.push_back({
                {dependency.type, dependency.reference},
                convert_dependency_kind(*kind),
            });
            ++kind;
        }
        variable.output_dependencies = std::move(dependencies);
    }
}

namespace
{
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
    validate_output_dependencies_xml(modelDescription, description, true);
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
