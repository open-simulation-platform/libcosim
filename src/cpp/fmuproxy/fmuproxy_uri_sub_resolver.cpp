
#include <cse/uri.hpp>
#include <cse/error.hpp>
#include <cse/fmuproxy/fmuproxy_client.hpp>
#include <cse/fmuproxy/fmuproxy_uri_sub_resolver.hpp>
#include <cse/fmuproxy/remote_fmu.hpp>

#include <string>

// examples of uri to resolve
//fmu-proxy://127.0.0.1:9090?guid=2213kjhlkh4lhksdkj
//fmu-proxy://127.0.0.1:9090?url=http://example.com/my_model.fmu
//fmu-proxy://127.0.0.1:9090?file=models/my_model.fmu

namespace
{

std::pair<std::string, unsigned int> parse_authority(std::string_view auth)
{
    auto colon = auth.find(":");
    auto host = std::string(auth.substr(0, colon));
    auto port = std::stoi(std::string(auth.substr(colon + 1)));
    return {host, port};
}

} // namespace

std::shared_ptr<cse::model> cse::fmuproxy::fmuproxy_uri_sub_resolver::lookup_model(const cse::uri& baseUri, const cse::uri& modelUriReference)
{
    auto query = modelUriReference.query();
    if (query && query->find("file=") < query->size()) {
        auto newQuery = "file=" + cse::file_uri_to_path(baseUri).parent_path().string() + "/" + std::string(query->substr(5));
        return model_uri_sub_resolver::lookup_model(baseUri, uri(modelUriReference.scheme(), modelUriReference.authority(), modelUriReference.path(), newQuery, modelUriReference.fragment()));
    }
    return model_uri_sub_resolver::lookup_model(baseUri, modelUriReference);
}

std::shared_ptr<cse::model> cse::fmuproxy::fmuproxy_uri_sub_resolver::lookup_model(const cse::uri& modelUri)
{

    assert(modelUri.scheme().has_value());
    if (*modelUri.scheme() != "fmu-proxy") return nullptr;
    if (!modelUri.authority().has_value()) return nullptr;
    if (!modelUri.query().has_value()) return nullptr;

    auto query = *modelUri.query();
    auto auth = parse_authority(*modelUri.authority());
    auto client = cse::fmuproxy::fmuproxy_client(auth.first, auth.second);

    if (query.substr(0, 5) == "guid=") {
        auto guid = std::string(query.substr(5));
        return client.from_guid(guid);
    } else if (query.substr(0, 5) == "file=") {
        auto file = std::string(query.substr(5));
        return client.from_file(file);
    } else if (query.substr(0, 4) == "url=") {
        auto url = std::string(query.substr(4));
        return client.from_url(url);
    } else {
        return nullptr;
    }
}
