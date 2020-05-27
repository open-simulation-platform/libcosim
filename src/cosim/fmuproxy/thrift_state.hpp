/**
 *  \file
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_FMUPROXY_SHARED_THRIFT_STATE_HPP
#define COSIM_FMUPROXY_SHARED_THRIFT_STATE_HPP

#include <cosim/fmuproxy/fmu_service.hpp>

#include <memory>

namespace cosim
{

namespace fmuproxy
{

class remote_fmu;
class fmuproxy_client;

class thrift_state
{

    friend class remote_fmu;
    friend class fmuproxy_client;

public:
    thrift_state(std::shared_ptr<::fmuproxy::thrift::fmu_service_if> client_,
        std::shared_ptr<apache::thrift::transport::TTransport> transport_);

    ::fmuproxy::thrift::fmu_service_if& client();

    apache::thrift::transport::TTransport& transport();

    ~thrift_state();

private:
    const std::shared_ptr<::fmuproxy::thrift::fmu_service_if> client_;
    const std::shared_ptr<apache::thrift::transport::TTransport> transport_;
};

} // namespace fmuproxy

} // namespace cosim

#endif
