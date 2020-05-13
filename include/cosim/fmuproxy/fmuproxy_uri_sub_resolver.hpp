/**
 *  \file
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_FMUPROXY_URI_SUB_RESOLVER_HPP
#define COSIM_FMUPROXY_URI_SUB_RESOLVER_HPP

#include <cosim/orchestration.hpp>

namespace cosim
{

namespace fmuproxy
{

/**
 *  Class for resolving fmu-proxy URI schemes.
 *
 *  Supports file, url and guid (pre-loaded on server) FMUs
 *
 *  From file: `fmu-proxy://127.0.0.1:9090?file=models/my_model.fmu`
 *  From url: `fmu-proxy://127.0.0.1:9090?url=http://example.com/my_model.fmu`
 *  From guid: `fmu-proxy://127.0.0.1:9090?guid={ads4ffsa4523fgg8}`
 */
class fmuproxy_uri_sub_resolver : public model_uri_sub_resolver
{

public:
    std::shared_ptr<model> lookup_model(const uri& baseUri, const uri& modelUriReference) override;

    std::shared_ptr<model> lookup_model(const cosim::uri& uri) override;
};

} // namespace fmuproxy

} // namespace cosim

#endif
