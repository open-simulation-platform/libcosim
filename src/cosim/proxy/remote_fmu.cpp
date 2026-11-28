/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include <cosim/model_description.hpp>
#include <cosim/proxy/remote_fmu.hpp>
#include <cosim/proxy/remote_slave.hpp>
#include <cosim/slave.hpp>

#include <proxyfmu/client/proxy_fmu.hpp>
#include <proxyfmu/fmi/model_description.hpp>
#include <proxyfmu/fmi/scalar_variable.hpp>
#include <proxyfmu/remote_info.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant> // NOLINT(misc-include-cleaner): Provides std::get for the variant type attributes.

using namespace proxyfmu;
using namespace proxyfmu::client;

namespace
{

cosim::variable_type get_type(const proxyfmu::fmi::scalar_variable& variable)
{
    if (variable.is_integer()) {
        return cosim::variable_type::integer;
    } else if (variable.is_real()) {
        return cosim::variable_type::real;
    } else if (variable.is_string()) {
        return cosim::variable_type::string;
    } else if (variable.is_boolean()) {
        return cosim::variable_type::boolean;
    } else {
        throw std::runtime_error("Unsupported variable type");
    }
}

cosim::variable_causality get_causality(const proxyfmu::fmi::scalar_variable& variable)
{
    if (variable.causality == "output") {
        return cosim::variable_causality::output;
    } else if (variable.causality == "input") {
        return cosim::variable_causality::input;
    } else if (variable.causality == "parameter") {
        return cosim::variable_causality::parameter;
    } else if (variable.causality == "calculated_parameter") {
        return cosim::variable_causality::calculated_parameter;
    } else if (variable.causality == "independent") {
        return cosim::variable_causality::independent;
    } else {
        return cosim::variable_causality::local;
    }
}

cosim::variable_variability get_variability(const proxyfmu::fmi::scalar_variable& variable)
{
    if (variable.variability == "discrete") {
        return cosim::variable_variability::discrete;
    } else if (variable.variability == "fixed") {
        return cosim::variable_variability::fixed;
    } else if (variable.variability == "tunable") {
        return cosim::variable_variability::tunable;
    } else if (variable.variability == "constant") {
        return cosim::variable_variability::constant;
    } else {
        return cosim::variable_variability::continuous;
    }
}

std::unique_ptr<cosim::model_description> parse_model_description(const proxyfmu::fmi::model_description& modelDescription)
{
    auto parsedDescription = std::make_unique<cosim::model_description>();
    parsedDescription->uuid = modelDescription.guid;
    parsedDescription->author = modelDescription.author;
    parsedDescription->name = modelDescription.modelName;
    parsedDescription->description = modelDescription.description;
    // ProxyFMU only initializes these state capability flags for FMI 2.
    if (modelDescription.fmiVersion == "2.0") {
        parsedDescription->capabilities.can_save_state = modelDescription.canGetAndSetFMUstate;
        parsedDescription->capabilities.can_export_state = modelDescription.canSerializeFMUstate;
    }

    for (const auto& variable : modelDescription.modelVariables) {
        cosim::variable_description description;
        description.name = variable.name;
        description.reference = variable.vr;
        description.type = get_type(variable);
        description.causality = get_causality(variable);
        description.variability = get_variability(variable);

        if (variable.is_real()) {
            const auto type = std::get<proxyfmu::fmi::real>(variable.typeAttribute);
            description.start = type.start;
        } else if (variable.is_integer()) {
            const auto type = std::get<proxyfmu::fmi::integer>(variable.typeAttribute);
            description.start = type.start;
        } else if (variable.is_boolean()) {
            const auto type = std::get<proxyfmu::fmi::boolean>(variable.typeAttribute);
            description.start = type.start;
        } else if (variable.is_string()) {
            const auto type = std::get<proxyfmu::fmi::string>(variable.typeAttribute);
            description.start = type.start;
        }

        parsedDescription->variables.push_back(description);
    }

    return parsedDescription;
}

} // namespace

cosim::proxy::remote_fmu::remote_fmu(const cosim::filesystem::path& fmuPath, const std::optional<remote_info>& remote)
    : fmu_(std::make_unique<proxy_fmu>(fmuPath, remote))
    , modelDescription_(parse_model_description(fmu_->get_model_description()))
{
}

std::shared_ptr<const cosim::model_description> cosim::proxy::remote_fmu::description() const noexcept
{
    return modelDescription_;
}

std::shared_ptr<cosim::slave> cosim::proxy::remote_fmu::instantiate(std::string_view instanceName)
{
    auto proxySlave = fmu_->new_instance(std::string(instanceName));
    return std::make_shared<remote_slave>(std::move(proxySlave), modelDescription_);
}
