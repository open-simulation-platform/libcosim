#include <cse/error.hpp>
#include <cse/fmuproxy/fmuproxy_client.hpp>
#include <cse/fmuproxy/thrift_state.hpp>
#include <cse/log/logger.hpp>

#include <boost/filesystem.hpp>

#include <cstdio>
#include <string>

#ifdef _MSC_VER
#    pragma warning(push)
#    pragma warning(disable : 4245 4706)
#endif

#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TSocketPool.h>
#include <thrift/transport/TTransportUtils.h>

#ifdef _MSC_VER
#    pragma warning(pop)
#endif

using namespace fmuproxy::thrift;
using namespace apache::thrift::transport;
using namespace apache::thrift::protocol;

namespace fs = boost::filesystem;

namespace
{

void read_data(const std::string& fileName, std::string& data)
{
    FILE* file = fopen(fileName.c_str(), "rb");
    if (file == nullptr) return;
    fseek(file, 0, SEEK_END);
    const auto size = ftell(file);
    fclose(file);

    file = fopen(fileName.c_str(), "rb");
    data.resize(size);
#if defined(__GNUC__)
    size_t read __attribute__((unused)) = fread(data.data(), sizeof(unsigned char), size, file);
#else
    fread(data.data(), sizeof(unsigned char), size, file);
#endif
    fclose(file);
}

} // namespace

cse::fmuproxy::fmuproxy_client::fmuproxy_client(const std::string& host, const unsigned int port,
    const bool concurrent)
{
    std::shared_ptr<TTransport> socket(new TSocket(host, port));
    std::shared_ptr<TTransport> transport(new TFramedTransport(socket));
    std::shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport));
    std::shared_ptr<::fmuproxy::thrift::fmu_service_if> client;
    if (!concurrent) {
        client = std::make_shared<fmu_service_client>(protocol);
    } else {
        client = std::make_shared<fmu_service_concurrent_client>(protocol);
    }
    try {
        transport->open();
    } catch (TTransportException&) {
        std::string msg = "Failed to connect to remote FMU @ " + host + ":" + std::to_string(port);
        CSE_PANIC_M(msg.c_str());
    }
    state_ = std::make_shared<thrift_state>(client, transport);
}

std::shared_ptr<cse::fmuproxy::remote_fmu>
cse::fmuproxy::fmuproxy_client::from_url(const std::string& url)
{
    BOOST_LOG_SEV(log::logger(), log::debug) << "fmu-proxy will load FMU from url '" << url << "'";
    FmuId fmuId;
    state_->client_->load_from_url(fmuId, url);
    return from_guid(fmuId);
}

std::shared_ptr<cse::fmuproxy::remote_fmu>
cse::fmuproxy::fmuproxy_client::from_file(const std::string& file)
{
    BOOST_LOG_SEV(log::logger(), log::debug) << "fmu-proxy will load FMU from file '" << file << "'";

    if (!fs::exists(file)) {
        std::string msg = "No such file '" + file + "'!";
        CSE_PANIC_M(msg.c_str());
    }

    const auto name = fs::path(file).stem().string();

    std::string data;
    read_data(file, data);

    FmuId fmuId;
    state_->client_->load_from_file(fmuId, name, data);
    return from_guid(fmuId);
}

std::shared_ptr<cse::fmuproxy::remote_fmu>
cse::fmuproxy::fmuproxy_client::from_guid(const std::string& guid)
{
    return std::make_shared<cse::fmuproxy::remote_fmu>(guid, state_);
}
