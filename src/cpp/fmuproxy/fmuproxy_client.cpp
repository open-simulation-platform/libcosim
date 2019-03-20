#include <cse/error.hpp>
#include <cse/fmuproxy/fmuproxy_client.hpp>
#include <cse/fmuproxy/thrift_state.hpp>

#include <boost/filesystem.hpp>

#include <cstdio>
#include <string>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TSocketPool.h>
#include <thrift/transport/TTransportUtils.h>

using namespace fmuproxy::thrift;
using namespace apache::thrift::transport;
using namespace apache::thrift::protocol;

namespace fs = boost::filesystem;

namespace
{

void read_data(std::string const &fileName, std::string &data)
{

    FILE* file = fopen(fileName.c_str(), "rb");
    if (file == NULL) return;
    fseek(file, 0, SEEK_END);
    long int size = ftell(file);
    fclose(file);
    file = fopen(fileName.c_str(), "rb");
#if defined(__GNUC__)
    size_t read __attribute__((unused)) = fread(data.data(), sizeof(unsigned char), size, file);
#else
    fread(data.data(), sizeof(unsigned char), size, file);
#endif
    data.resize(size);

    fclose(file);
}

} // namespace

cse::fmuproxy::fmuproxy_client::fmuproxy_client(const std::string& host, const unsigned int port, const bool concurrent)
{
    std::shared_ptr<TTransport> socket(new TSocket(host, port));
    std::shared_ptr<TTransport> transport(new TFramedTransport(socket));
    std::shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport));
    std::shared_ptr<::fmuproxy::thrift::FmuServiceIf> client;
    if (!concurrent) {
        client = std::make_shared<FmuServiceClient>(protocol);
    } else {
        client = std::make_shared<FmuServiceConcurrentClient>(protocol);
    }
    try {
        transport->open();
    } catch (TTransportException&) {
        std::string msg = "Failed to connect to remote FMU @ " + host + ":" + std::to_string(port);
        CSE_PANIC_M(msg.c_str());
    }
    state_ = std::make_shared<thrift_state>(client, transport);
}

cse::fmuproxy::remote_fmu
cse::fmuproxy::fmuproxy_client::from_url(const std::string& url)
{
    FmuId fmuId;
    state_->client_->loadFromUrl(fmuId, url);
    return from_guid(fmuId);
}

cse::fmuproxy::remote_fmu
cse::fmuproxy::fmuproxy_client::from_file(const std::string& file)
{

    const auto name = fs::path(file).stem().string();

    std::string data;
    read_data(file, data);

    FmuId fmuId;
    state_->client_->loadFromFile(fmuId, name, data);
    return from_guid(fmuId);
}

cse::fmuproxy::remote_fmu
cse::fmuproxy::fmuproxy_client::from_guid(const std::string& guid)
{
    return cse::fmuproxy::remote_fmu(guid, state_);
}
