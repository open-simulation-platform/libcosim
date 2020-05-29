/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include <cosim/fmuproxy/thrift_state.hpp>

#include <utility>

cosim::fmuproxy::thrift_state::thrift_state(std::shared_ptr<::fmuproxy::thrift::FmuServiceIf> client_,
    std::shared_ptr<apache::thrift::transport::TTransport> transport_)
    : client_(std::move(client_))
    , transport_(std::move(transport_))
{ }

::fmuproxy::thrift::FmuServiceIf& cosim::fmuproxy::thrift_state::client()
{
    return *client_;
}
apache::thrift::transport::TTransport& cosim::fmuproxy::thrift_state::transport()
{
    return *transport_;
}

cosim::fmuproxy::thrift_state::~thrift_state()
{
    if (transport_->isOpen()) {
        transport_->close();
    }
}
