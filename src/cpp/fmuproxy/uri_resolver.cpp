
#include <cse/error.hpp>
#include <cse/fmuproxy/client.hpp>
#include <cse/fmuproxy/remote_fmu.hpp>
#include <cse/fmuproxy/uri_resolver.hpp>

#include <string>

// examples of uri to resolve
//fmu-proxy://127.0.0.1:9090?guid=2213kjhlkh4lhksdkj
//fmu-proxy://127.0.0.1:9090?url=http://example.com/my_model.fmu
//fmu-proxy://127.0.0.1:9090?file=models/my_model.fmu

std::shared_ptr<cse::model> cse::fmuproxy::fmuproxy_sub_resolver::lookup_model(std::string_view uri)
{

    if (uri.substr(0, 8) != "fmu-proxy://") return nullptr;

    uri = uri.substr(12); //skip fmu-proxy://
    auto colon = uri.find(":");
    auto question = uri.find("?");

    std::string host = uri.substr(12, colon).data();
    unsigned int port = std::stoi(uri.substr(colon + 1, question).data());

    uri = uri.substr(question + 1); //skip host:port?
    auto client = cse::fmuproxy::client(host, port, true);
    if (uri.substr(0, 5) == "guid=") {
        auto guid = std::string(uri.substr(5));
        return client.from_guid(guid);
    } else if (uri.substr(0, 5) == "file=") {
        auto file = std::string(uri.substr(5));
        return client.from_guid(file);
    } else if (uri.substr(0, 4) == "url=") {
        auto url = std::string(uri.substr(4));
        return client.from_url(url);
    } else {
        CSE_PANIC();
    }
}
