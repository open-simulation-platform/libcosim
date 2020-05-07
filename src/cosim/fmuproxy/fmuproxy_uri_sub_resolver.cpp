
#include <cosim/error.hpp>
#include <cosim/fmuproxy/fmuproxy_client.hpp>
#include <cosim/fmuproxy/fmuproxy_uri_sub_resolver.hpp>
#include <cosim/fmuproxy/remote_fmu.hpp>
#include <cosim/uri.hpp>

#include <string>

// examples of uri to resolve
//fmu-proxy://127.0.0.1:9090?guid=2213kjhlkh4lhksdkj
//fmu-proxy://127.0.0.1:9090?url=http://example.com/my_model.fmu
//fmu-proxy://127.0.0.1:9090?file=models/my_model.fmu

namespace
{

std::pair<std::string, unsigned int> parse_authority(std::string_view auth)
{
    const auto colon = auth.find(":");
    const auto host = std::string(auth.substr(0, colon));
    const auto port = std::stoi(std::string(auth.substr(colon + 1)));
    return {host, port};
}

} // namespace

std::shared_ptr<cosim::model> cosim::fmuproxy::fmuproxy_uri_sub_resolver::lookup_model(
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

std::shared_ptr<cosim::model> cosim::fmuproxy::fmuproxy_uri_sub_resolver::lookup_model(const cosim::uri& modelUri)
{
    assert(modelUri.scheme().has_value());
    if (*modelUri.scheme() != "fmu-proxy") return nullptr;
    COSIM_INPUT_CHECK(modelUri.authority());
    COSIM_INPUT_CHECK(modelUri.query());

    const auto query = *modelUri.query();
    const auto auth = parse_authority(*modelUri.authority());
    auto client = cosim::fmuproxy::fmuproxy_client(auth.first, auth.second);

    if (query.substr(0, 5) == "guid=") {
        const auto guid = std::string(query.substr(5));
        return client.from_guid(guid);
    } else if (query.substr(0, 5) == "file=") {
        const auto file = std::string(query.substr(5));
        return client.from_file(file);
    } else if (query.substr(0, 4) == "url=") {
        const auto url = std::string(query.substr(4));
        return client.from_url(url);
    } else {
        return nullptr;
    }
}
