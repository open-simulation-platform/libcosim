
#include <cse/error.hpp>
#include <cse/fmuproxy/remote_fmu.hpp>
#include <cse/fmuproxy/remote_slave.hpp>

#include <thrift/transport/TSocketPool.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TTransportUtils.h>

#include "cse/fmuproxy/fmuproxy_helper.hpp"

using namespace fmuproxy::thrift;
using namespace apache::thrift::transport;
using namespace apache::thrift::protocol;

namespace {

    std::shared_ptr<cse::model_description> getModelDescription(FmuServiceIf &client, FmuId &fmuId) {
        ::fmuproxy::thrift::ModelDescription md = ModelDescription();
        client.getModelDescription(md, fmuId);
        return convert(md);
    }

}

cse::fmuproxy::remote_fmu::remote_fmu(
        const FmuId &fmuId,
        std::shared_ptr<TTransport> transport,
        std::shared_ptr<FmuServiceIf> client) : fmuId_(fmuId), client_(client), transport_(transport) {
    modelDescription_ = getModelDescription(*client_, fmuId_);
}


cse::fmuproxy::remote_fmu
cse::fmuproxy::remote_fmu::from_url(const std::string &url,
                                    const std::string &host,
                                    const unsigned int port,
                                    const bool concurrent) {
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
    } catch (TTransportException &ex) {
        std::string msg = "Failed to connect to remote FMU @ " + host + ":" + std::to_string(port);
        CSE_PANIC_M(msg.c_str());
    }
    std::string fmuId;
    client->load(fmuId, url);
    return cse::fmuproxy::remote_fmu(fmuId, transport, client);
}

cse::fmuproxy::remote_fmu
cse::fmuproxy::remote_fmu::from_guid(const std::string &guid,
                                     const std::string &host,
                                     const unsigned int port,
                                     const bool concurrent) {
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
    } catch (TTransportException &ex) {
        std::string msg = "Failed to connect to remote FMU @ " + host + ":" + std::to_string(port);
        CSE_PANIC_M(msg.c_str());
    }
    return cse::fmuproxy::remote_fmu(guid, transport, client);
}


std::shared_ptr<const cse::model_description> cse::fmuproxy::remote_fmu::model_description() const {
    return modelDescription_;
}

std::shared_ptr<cse::slave> cse::fmuproxy::remote_fmu::instantiate_slave() {
    InstanceId instanceId;
    client_->createInstanceFromCS(instanceId, fmuId_);
    std::shared_ptr<cse::slave> slave(new cse::fmuproxy::remote_slave(instanceId, client_, modelDescription_));
    return slave;
}

cse::fmuproxy::remote_fmu::~remote_fmu() {
    transport_->close();
}
