/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/ssp/ssp_loader.hpp"

#include "cosim/exception.hpp"
#include "cosim/function/description.hpp"
#include "cosim/function/linear_transformation.hpp"
#include "cosim/log/logger.hpp"
#include "cosim/model_description.hpp"
#include "cosim/orchestration.hpp"
#include "cosim/ssp/ssp_parser.hpp"
#include "cosim/system_structure.hpp"
#include "cosim/time.hpp"
#include "cosim/uri.hpp"
#include "cosim/utility/filesystem.hpp"
#include "cosim/utility/zip.hpp"
#include "cosim/xml_clock.hpp"
#include "cosim/xml_value.hpp"

#include <boost/log/sources/severity_feature.hpp>
#include <boost/range/algorithm/find_if.hpp>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>


namespace cosim
{


ssp_loader::ssp_loader()
    : modelResolver_(cosim::default_model_uri_resolver())
{ }

void ssp_loader::set_model_uri_resolver(std::shared_ptr<cosim::model_uri_resolver> resolver)
{
    modelResolver_ = std::move(resolver);
}

void ssp_loader::set_ssd_file_name(const std::string& name)
{
    ssdFileName_ = name;
}

namespace
{
void validate_connector(
    const std::string& component,
    const ssp_parser::Connector& connector,
    const system_structure& structure)
{
    const full_variable_name name(component, connector.name);
    const auto& variable = structure.get_variable_description(name);
    const auto rejection = [&](const std::string& reason) {
        return error(make_error_code(errc::invalid_system_structure),
            "SSP connector '" + to_text(name) + "': " + reason);
    };
    if (connector.type && *connector.type != variable.type) {
        throw rejection("declared type " + std::string(to_text(*connector.type)) +
            " does not match FMU type " + std::string(to_text(variable.type)));
    }
    const auto shape = fixed_shape(variable);
    if (!shape || *shape != connector.shape) {
        throw rejection("declared shape does not match the fixed FMU shape");
    }
    if (connector.clockAttributes.empty()) {
        return;
    }
    if (!variable.clock) {
        throw rejection("FMU Clock metadata is unavailable");
    }
    const auto& clock = variable.clock.value();
    const auto get = [&](const char* attribute) -> std::optional<std::string_view> {
        const auto found = connector.clockAttributes.find(attribute);
        if (found == connector.clockAttributes.end()) {
            return std::nullopt;
        }
        return found->second;
    };
    const auto check = [&](const char* attribute, auto actual) {
        decltype(actual) declared;
        try {
            declared = detail::xml_clock_attribute<typename decltype(actual)::value_type>(get, attribute);
        } catch (const std::logic_error& e) {
            throw rejection(std::string(attribute) + ": " + e.what());
        }
        if (declared && declared != actual) {
            throw rejection(std::string(attribute) + " does not match FMU Clock metadata");
        }
    };
    check("intervalVariability", std::optional(clock.interval_variability));
    check("intervalDecimal", clock.interval_decimal);
    check("shiftDecimal", std::optional(clock.shift_decimal.value_or(0.0)));
    check("intervalCounter", clock.interval_counter);
    check("resolution", clock.resolution);
    check("shiftCounter", std::optional(clock.shift_counter.value_or(0)));
    check("supportsFraction", std::optional(clock.supports_fraction));
    check("priority", clock.priority);
    check("canBeDeactivated", std::optional(clock.can_be_deactivated));
}

variable_value resolve_parameter(
    const ssp_parser::Parameter& parameter,
    const variable_description& variable,
    const model_description& model)
{
    if (const auto* const value = std::get_if<variable_value>(&parameter.value)) {
        return *value;
    }
    const auto& symbolic = std::get<ssp_parser::EnumerationParameter>(parameter.value);
    if (variable.type != variable_type::enumeration || !variable.declared_type) {
        throw std::invalid_argument("SSP Enumeration requires an FMU Enumeration with a declared type");
    }
    const auto definition = boost::range::find_if(model.type_definitions,
        [&](const auto& type) { return type.name == *variable.declared_type && type.type == variable_type::enumeration; });
    if (definition == model.type_definitions.end()) {
        throw std::invalid_argument("FMU Enumeration type definition not found");
    }
    const auto parseItem = [&](variable_type, std::string_view name) -> scalar_value {
        const auto item = std::find_if(definition->enumeration_items.begin(), definition->enumeration_items.end(),
            [&](const auto& entry) { return entry.name == name; });
        if (item == definition->enumeration_items.end()) {
            throw std::invalid_argument("Unknown FMU Enumeration item '" + std::string(name) + "'");
        }
        return enumeration_value{item->value};
    };
    return detail::parse_xml_value(variable_type::enumeration, symbolic.attribute,
        {symbolic.items.begin(), symbolic.items.end()}, symbolic.shape, parseItem);
}

// Adds an SSP component parameter set to a `variable_value_map`.
void add_parameter_set(
    variable_value_map& valueMap,
    const std::string& componentName,
    const ssp_parser::ParameterSet& parameterSet,
    const system_structure& systemStructure,
    const model_description& model)
{
    for (const auto& param : parameterSet.parameters) {
        try {
            const auto variableName = full_variable_name(componentName, param.name);
            const auto& variableDescription =
                systemStructure.get_variable_description(variableName);
            if (variableDescription.causality != variable_causality::parameter &&
                variableDescription.causality != variable_causality::input) {
                throw std::runtime_error(
                    "Non-input causality for variable " + to_text(variableName));
            }
            add_variable_value(
                valueMap,
                systemStructure,
                variableName,
                resolve_parameter(param, variableDescription, model));
        } catch (const std::exception& e) {
            // SSP allows ignoring failures when applying parameter sets to
            // components.  We may want to restrict to only specific failures
            // in the future, though.
            BOOST_LOG_SEV(log::logger(), log::warning)
                << "SSP parameter set " << parameterSet.name << ": "
                << e.what();
        }
    }
}

void add_connection(
    system_structure& structure,
    const ssp_parser::Connection& connection,
    const ssp_parser& parser)
{
    const auto output = full_variable_name(connection.start.component, connection.start.connector);
    const auto input = full_variable_name(connection.end.component, connection.end.connector);

    if (const auto& transformation = connection.linearTransformation) {
        if (parser.has_exact_types()) {
            const auto isScalarReal = [&](const full_variable_name& name) {
                const auto& variable = structure.get_variable_description(name);
                return variable.type == variable_type::real && variable.dimensions.empty();
            };
            if (!isScalarReal(output) || !isScalarReal(input)) {
                throw error(make_error_code(errc::invalid_system_structure),
                    "SSP LinearTransformation requires scalar Real connectors");
            }
        }
        const auto functionName =
            "__linearTransformation__" + std::to_string(std::random_device{}());

        function_parameter_value_map functionParams;
        functionParams[linear_transformation_function_type::offset_parameter_index] = transformation->offset;
        functionParams[linear_transformation_function_type::factor_parameter_index] = transformation->factor;

        structure.add_entity(
            functionName,
            std::make_shared<linear_transformation_function_type>(),
            functionParams);
        structure.connect_variables(
            output,
            full_variable_name(functionName, "in", ""));
        structure.connect_variables(
            full_variable_name(functionName, "out", ""),
            input);
    } else {
        structure.connect_variables(output, input);
    }
}
} // namespace

ssp_configuration ssp_loader::load(const cosim::filesystem::path& configPath)
{
    std::optional<time_point> ignoredEndTime;
    return load(configPath, ignoredEndTime);
}

ssp_configuration ssp_loader::load(
    const cosim::filesystem::path& configPath,
    std::optional<time_point>& endTime)
{
    const auto modelResolver = modelResolver_;
    const auto ssdFileName = ssdFileName_.value_or("SystemStructure");
    auto sspFile = configPath;
    std::optional<cosim::utility::temp_dir> temp_ssp_dir;
    if (sspFile.extension() == ".ssp") {
        temp_ssp_dir = cosim::utility::temp_dir();
        const auto archive = cosim::utility::zip::archive(sspFile);
        archive.extract_all(temp_ssp_dir->path());
        sspFile = temp_ssp_dir->path();
    }

    const auto absolutePath = cosim::filesystem::absolute(sspFile);
    const auto configFile = cosim::filesystem::is_regular_file(absolutePath)
        ? absolutePath
        : absolutePath / std::string(ssdFileName + ".ssd");
    const auto baseURI = path_to_file_uri(configFile);
    const auto parser = ssp_parser(configFile);
    const auto stopTime = parser.get_default_experiment().stopTime;

    ssp_configuration configuration;
    configuration.start_time = get_default_start_time(parser);
    configuration.algorithm = parser.get_default_experiment().algorithm;
    configuration.parameter_sets[""]; // Ensure that the default set exists.

    for (const auto& [componentName, component] : parser.get_elements()) {
        const auto model = modelResolver->lookup_model(baseURI, component.source);
        configuration.system_structure.add_entity(
            componentName,
            model,
            cosim::to_duration(component.stepSizeHint.value_or(0)));
        if (parser.has_exact_types()) {
            for (const auto& entry : component.connectors) {
                validate_connector(componentName, entry.second, configuration.system_structure);
            }
        }
        if (component.parameterSets.empty()) {
            continue;
        }
        const auto description = model->description();
        if (!description) {
            throw error(make_error_code(errc::invalid_system_structure),
                "Model description is unavailable for SSP component " + componentName);
        }

        for (const auto& paramSet : component.parameterSets) {
            add_parameter_set(
                configuration.parameter_sets[paramSet.name],
                componentName,
                paramSet,
                configuration.system_structure, *description);
        }
        add_parameter_set(
            configuration.parameter_sets[""],
            componentName,
            component.parameterSets.at(0),
            configuration.system_structure, *description);
    }

    for (const auto& connection : parser.get_connections()) {
        add_connection(configuration.system_structure, connection, parser);
    }
    endTime = stopTime ? std::optional<time_point>(to_time_point(*stopTime)) : std::nullopt;
    return configuration;
}

} // namespace cosim
