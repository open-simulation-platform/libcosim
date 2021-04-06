/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include <cosim/error.hpp>
#include <cosim/proxy/remote_fmu.hpp>
#include <cosim/proxy/remote_slave.hpp>

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

cosim::variable_causality get_causality(const proxyfmu::fmi::scalar_variable&)
{
    //TODO
    return cosim::variable_causality::local;
}

cosim::variable_variability get_variability(const proxyfmu::fmi::scalar_variable&)
{
    //TODO
    return cosim::variable_variability::continuous;
}

std::unique_ptr<cosim::model_description> parse_model_description(const proxyfmu::fmi::model_description& md)
{
    auto _md = std::make_unique<cosim::model_description>();
    _md->uuid = md.guid;
    _md->name = md.model_name;
    _md->description = md.description;
    _md->author = md.author;

    auto vars = md.model_variables;
    for (auto& var : vars) {
        cosim::variable_description vd;
        vd.name = var.name;
        vd.reference = var.vr;
        vd.type = get_type(var);
        vd.causality = get_causality(var);
        vd.variability = get_variability(var);
        _md->variables.push_back(vd);
    }

    return _md;
}

} // namespace

cosim::proxy::remote_fmu::remote_fmu(const std::filesystem::path& fmuPath)
    : fmu_(proxyfmu::fmi::loadFmu(fmuPath))
    , modelDescription_(parse_model_description(fmu_->get_model_description()))
{
}

std::shared_ptr<const cosim::model_description> cosim::proxy::remote_fmu::description() const noexcept
{
    return modelDescription_;
}

std::shared_ptr<cosim::async_slave> cosim::proxy::remote_fmu::instantiate(std::string_view /*instanceName*/)
{
    auto slave = std::make_shared<remote_slave>(fmu_->new_instance(""), modelDescription_);
    return cosim::make_background_thread_slave(slave);
}
