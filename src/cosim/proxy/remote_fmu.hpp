/**
 *  \file
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_PROXY_REMOTE_FMU_HPP
#define COSIM_PROXY_REMOTE_FMU_HPP

#include <cosim/async_slave.hpp>
#include <cosim/model_description.hpp>
#include <cosim/orchestration.hpp>

#include <filesystem>
#include <memory>
#include <proxyfmu/fmi/fmu.hpp>

namespace cosim
{

namespace proxy
{

class remote_fmu : public cosim::model
{

public:
    explicit remote_fmu(const std::filesystem::path& fmuPath);

    std::shared_ptr<const cosim::model_description> description() const noexcept override;

    std::shared_ptr<cosim::async_slave> instantiate(std::string_view name) override;

private:
    std::unique_ptr<proxyfmu::fmi::fmu> fmu_;
    std::shared_ptr<const cosim::model_description> modelDescription_;
};

} // namespace proxy

} // namespace cosim

#endif
