/**
 *  \file
 *  Defines the FMU-proxy client
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_FMUPROXY_FMUPROXYCLIENT_HPP
#define COSIM_FMUPROXY_FMUPROXYCLIENT_HPP

#include "FmuService.h"

#include <cosim/fmuproxy/remote_fmu.hpp>

#include <memory>

namespace cosim
{

namespace fmuproxy
{

class fmuproxy_client
{

public:
    fmuproxy_client(
        const std::string& host,
        unsigned int port);

    std::shared_ptr<remote_fmu> from_url(const std::string& url);

    std::shared_ptr<remote_fmu> from_file(const std::string& file);

    std::shared_ptr<remote_fmu> from_guid(const std::string& guid);

private:
    std::shared_ptr<thrift_state> state_;
};

} // namespace fmuproxy

} // namespace cosim


#endif
