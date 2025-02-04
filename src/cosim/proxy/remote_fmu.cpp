/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include <cosim/error.hpp>
#include <cosim/proxy/remote_fmu.hpp>
#include <cosim/proxy/remote_slave.hpp>

#include <proxyfmu/client/proxy_fmu.hpp>

using namespace proxyfmu;
using namespace proxyfmu::client;

namespace
{

cosim::variable_type get_type(const proxyfmu::fmi::scalar_variable& v)
{
    if (v.is_integer()) {
        return cosim::variable_type::integer;
    } else if (v.is_real()) {
        return cosim::variable_type::real;
    } else if (v.is_string()) {
        return cosim::variable_type::string;
    } else if (v.is_boolean()) {
        return cosim::variable_type::boolean;
    } else {
        throw std::runtime_error("Unsupported variable type");
    }
}

cosim::variable_causality get_causality(const proxyfmu::fmi::scalar_variable& v)
{
    if (v.causality == "output") {
        return cosim::variable_causality::output;
    } else if (v.causality == "input") {
        return cosim::variable_causality::input;
    } else if (v.causality == "parameter") {
        return cosim::variable_causality::parameter;
    } else if (v.causality == "calculated_parameter") {
        return cosim::variable_causality::calculated_parameter;
    } else if (v.causality == "local") {
        return cosim::variable_causality::local;
    } else {
        return cosim::variable_causality::local;
    }
}

cosim::variable_variability get_variability(const proxyfmu::fmi::scalar_variable& v)
{
    if (v.variability == "discrete") {
        return cosim::variable_variability::discrete;
    } else if (v.variability == "fixed") {
        return cosim::variable_variability::fixed;
    } else if (v.variability == "tunable") {
        return cosim::variable_variability::tunable;
    } else if (v.variability == "constant") {
        return cosim::variable_variability::constant;
    } else if (v.variability == "continuous") {
        return cosim::variable_variability::continuous;
    } else {
        return cosim::variable_variability::continuous;
    }
}

std::unique_ptr<cosim::model_description> parse_model_description(const proxyfmu::fmi::model_description& md)
{
    auto _md = std::make_unique<cosim::model_description>();
    _md->uuid = md.guid;
    _md->author = md.author;
    _md->name = md.modelName;
    _md->description = md.description;
    _md->capabilities.can_get_and_set_fmu_state = md.canGetAndSetFMUstate;
    _md->capabilities.can_serialize_fmu_state = md.canSerializeFMUstate;

    for (auto& var : md.modelVariables) {
        cosim::variable_description vd;
        vd.name = var.name;
        vd.reference = var.vr;
        vd.type = get_type(var);
        vd.causality = get_causality(var);
        vd.variability = get_variability(var);

        if (var.is_real()) {
            const auto type = std::get<proxyfmu::fmi::real>(var.typeAttribute);
            vd.start = type.start;
        } else if (var.is_integer()) {
            const auto type = std::get<proxyfmu::fmi::integer>(var.typeAttribute);
            vd.start = type.start;
        } else if (var.is_boolean()) {
            const auto type = std::get<proxyfmu::fmi::boolean>(var.typeAttribute);
            vd.start = type.start;
        } else if (var.is_string()) {
            const auto type = std::get<proxyfmu::fmi::string>(var.typeAttribute);
            vd.start = type.start;
        }

        _md->variables.push_back(vd);
    }

    return _md;
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
