/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/ssp/ssp_loader.hpp"

#include "cosim/function/linear_transformation.hpp"
#include "cosim/log/logger.hpp"
#include "cosim/ssp/ssp_parser.hpp"
#include "cosim/utility/filesystem.hpp"
#include "cosim/utility/zip.hpp"

#include <random>


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
// Adds an SSP component parameter set to a `variable_value_map`.
void add_parameter_set(
    variable_value_map& valueMap,
    const std::string& componentName,
    const ssp_parser::ParameterSet& parameterSet,
    const system_structure& systemStructure)
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
                full_variable_name(componentName, param.name),
                param.value);
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
} // namespace

ssp_configuration ssp_loader::load(const cosim::filesystem::path& configPath)
{
    auto sspFile = configPath;
    std::optional<cosim::utility::temp_dir> temp_ssp_dir;
    if (sspFile.extension() == ".ssp") {
        temp_ssp_dir = cosim::utility::temp_dir();
        auto archive = cosim::utility::zip::archive(sspFile);
        archive.extract_all(temp_ssp_dir->path());
        sspFile = temp_ssp_dir->path();
    }

    const auto ssdFileName = ssdFileName_ ? *ssdFileName_ : "SystemStructure";
    const auto absolutePath = cosim::filesystem::absolute(sspFile);
    const auto configFile = cosim::filesystem::is_regular_file(absolutePath)
        ? absolutePath
        : absolutePath / std::string(ssdFileName + ".ssd");
    const auto baseURI = path_to_file_uri(configFile);
    const auto parser = ssp_parser(configFile);

    ssp_configuration configuration;
    configuration.start_time = get_default_start_time(parser);
    configuration.algorithm = parser.get_default_experiment().algorithm;
    configuration.parameter_sets[""]; // Ensure that the default set exists.

    auto elements = parser.get_elements();
    for (const auto& [componentName, component] : elements) {
        configuration.system_structure.add_entity(
            componentName,
            modelResolver_->lookup_model(baseURI, component.source),
            cosim::to_duration(component.stepSizeHint.value_or(0)));

        for (const auto& paramSet : component.parameterSets) {
            add_parameter_set(
                configuration.parameter_sets[paramSet.name],
                componentName,
                paramSet,
                configuration.system_structure);
        }
        if (!component.parameterSets.empty()) {
            add_parameter_set(
                configuration.parameter_sets[""],
                componentName,
                component.parameterSets[0],
                configuration.system_structure);
        }
    }

    for (const auto& connection : parser.get_connections()) {
        const auto output = full_variable_name(
            connection.startElement.name,
            connection.startConnector.name);
        const auto input = full_variable_name(
            connection.endElement.name,
            connection.endConnector.name);

        if (const auto& l = connection.linearTransformation) {
            const auto functionName =
                "__linearTransformation__" + std::to_string(std::random_device{}());

            function_parameter_value_map functionParams;
            functionParams[linear_transformation_function_type::offset_parameter_index] = l->offset;
            functionParams[linear_transformation_function_type::factor_parameter_index] = l->factor;

            configuration.system_structure.add_entity(
                functionName,
                std::make_shared<linear_transformation_function_type>(),
                functionParams);
            configuration.system_structure.connect_variables(
                output,
                full_variable_name(functionName, "in", ""));
            configuration.system_structure.connect_variables(
                full_variable_name(functionName, "out", ""),
                input);
        } else {
            configuration.system_structure.connect_variables(output, input);
        }
    }
    return configuration;
}

} // namespace cosim
