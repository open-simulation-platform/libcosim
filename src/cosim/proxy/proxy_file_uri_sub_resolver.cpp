/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include <cosim/proxy/proxy_file_uri_sub_resolver.hpp>
#include <cosim/proxy/remote_fmu.hpp>

#include "cosim/log/logger.hpp"

#include <filesystem>

#include <string>

std::shared_ptr<cosim::model> cosim::proxy::proxy_file_uri_sub_resolver::lookup_model(const cosim::uri& modelUri)
{
    assert(modelUri.scheme().has_value());
    if (*modelUri.scheme() != "proxy-file") return nullptr;
    if (modelUri.authority().has_value() &&
        !(modelUri.authority()->empty() || *modelUri.authority() == "localhost")) {
        return nullptr;
    }
    if (modelUri.query().has_value() || modelUri.fragment().has_value()) {
        BOOST_LOG_SEV(log::logger(), log::warning)
            << "Query and/or fragment component(s) in a file:// URI were ignored: "
            << modelUri;
    }
    const auto path = file_uri_to_path(modelUri);
    if (path.extension() != ".fmu") return nullptr;
    return std::make_shared<remote_fmu>(std::filesystem::path(path.string()));
}
