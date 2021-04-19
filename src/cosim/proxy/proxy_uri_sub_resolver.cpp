/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/log/logger.hpp"
#include <cosim/error.hpp>
#include <cosim/proxy/proxy_uri_sub_resolver.hpp>
#include <cosim/proxy/remote_fmu.hpp>

std::pair<std::string, int> parse_authority(std::string_view auth)
{
    const auto colonIdx = auth.find(':');
    if (colonIdx == std::string::npos) {
        return {std::string(auth), -1};
    } else {
        const auto host = std::string(auth.substr(0, colonIdx));
        const auto port = std::stoi(std::string(auth.substr(colonIdx + 1)));
        return {host, port};
    }
}

std::shared_ptr<cosim::model> cosim::proxy::proxy_uri_sub_resolver::lookup_model(
    const cosim::uri& baseUri,
    const cosim::uri& modelUriReference)
{
    const auto& mur = modelUriReference;
    const auto query = mur.query();
    if (query) {
        if (query->find("file=file:///") < query->size()) {
            const auto newQuery = "file=" + std::string(query->substr(13));
            return model_uri_sub_resolver::lookup_model(
                baseUri, uri(mur.scheme(), mur.authority(), mur.path(), newQuery, mur.fragment()));
        } else if (query->find("file=") < query->size()) {
            const auto pathToAppend = cosim::file_uri_to_path(baseUri).parent_path().string();
            const auto newQuery = "file=" + pathToAppend + "/" + std::string(query->substr(5));
            return model_uri_sub_resolver::lookup_model(
                baseUri, uri(mur.scheme(), mur.authority(), mur.path(), newQuery, mur.fragment()));
        }
    }
    return model_uri_sub_resolver::lookup_model(baseUri, mur);
}

std::shared_ptr<cosim::model> cosim::proxy::proxy_uri_sub_resolver::lookup_model(const cosim::uri& modelUri)
{
    assert(modelUri.scheme().has_value());
    if (*modelUri.scheme() != "proxyfmu") return nullptr;
    COSIM_INPUT_CHECK(modelUri.authority());
    COSIM_INPUT_CHECK(modelUri.query());

    const auto auth = parse_authority(*modelUri.authority());
    const auto query = *modelUri.query();
    if (query.substr(0, 5) == "file=") {
        const auto file = proxyfmu::filesystem::path(std::string(query.substr(5)));
        assert(proxyfmu::filesystem::exists(file));
        if (auth.first == "localhost" && auth.second == -1) {
            return std::make_shared<remote_fmu>(file);
        } else {
            return std::make_shared<remote_fmu>(file, proxyfmu::remote_info(auth.first, auth.second));
        }

    } else if (query.substr(0, 4) == "url=") {
        //TODO
        return nullptr;
    } else {
        return nullptr;
    }
}
