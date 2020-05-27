/**
 *  \file
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_FMUPROXY_REMOTE_FMU_HPP
#define COSIM_FMUPROXY_REMOTE_FMU_HPP

#ifdef _WIN32
//must be included before <windows.h>
#    include <winsock2.h>
#endif

#include <cosim/async_slave.hpp>
#include <cosim/fmuproxy/fmu_service.hpp>
#include <cosim/fmuproxy/thrift_state.hpp>
#include <cosim/model_description.hpp>
#include <cosim/orchestration.hpp>

#include <memory>

namespace cosim
{

namespace fmuproxy
{

class fmuproxy_client;

class remote_fmu : public cosim::model
{

    friend class fmuproxy_client;

public:
    remote_fmu(const ::fmuproxy::thrift::FmuId& fmuId,
        std::shared_ptr<thrift_state> state);

    std::shared_ptr<const cosim::model_description> description() const noexcept override;

    std::shared_ptr<cosim::slave> instantiate_slave();

    std::shared_ptr<cosim::async_slave> instantiate(std::string_view name = "") override;

private:
    const std::string fmuId_;
    std::shared_ptr<thrift_state> state_;
    std::shared_ptr<const cosim::model_description> modelDescription_;
};

} // namespace fmuproxy

} // namespace cosim

#endif
