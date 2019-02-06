
#include <cse/error.hpp>
#include <cse/fmuproxy/remote_fmu.hpp>
#include <cse/fmuproxy/remote_slave.hpp>

#include <thrift/transport/TSocketPool.h>
#include <thrift/protocol/TCompactProtocol.h>
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

cse::fmuproxy::remote_fmu::remote_fmu(FmuId fmuId, std::string host, unsigned int port, bool concurrent): fmuId_(fmuId) {
    std::shared_ptr<TTransport> socket(new TSocketPool(host, port));
    transport_ = std::make_shared<TFramedTransport>(socket);
    std::shared_ptr<TProtocol> protocol(new TCompactProtocol(transport_));
    if (!concurrent) {
        client_ = std::make_shared<FmuServiceClient>(protocol);
    } else {
        client_ = std::make_shared<FmuServiceConcurrentClient>(protocol);
    }
    try  {
        transport_->open();
    } catch(TTransportException ex) {
        std::string msg = "Failed to connect to remote FMU @ " + host + ":" + std::to_string(port);
        CSE_PANIC_M(msg.c_str());
    }
    modelDescription_ = getModelDescription(*client_, fmuId_);
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
