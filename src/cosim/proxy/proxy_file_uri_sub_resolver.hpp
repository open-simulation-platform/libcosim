/**
 *  \file
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_PROXY_FILE_URI_SUB_RESOLVER_HPP
#define COSIM_PROXY_FILE_URI_SUB_RESOLVER_HPP

#include <cosim/orchestration.hpp>

namespace cosim
{

namespace proxy
{

/**
 *  Class for resolving fmu-proxy URI schemes.
 *
 *  From file: 'proxy-file:///models/my_model.fmu'
 */
class proxy_file_uri_sub_resolver : public model_uri_sub_resolver
{

public:
    std::shared_ptr<model> lookup_model(const cosim::uri& uri) override;
};

} // namespace proxy

} // namespace cosim

#endif
