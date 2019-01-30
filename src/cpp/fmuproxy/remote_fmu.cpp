
#include <utility>

#include <cse/fmuproxy/remote_fmu.hpp>
#include <cse/fmuproxy/remote_slave.hpp>

#include "cse/fmuproxy/fmuproxy_helper.hpp"

using namespace fmuproxy::thrift;
using namespace apache::thrift::transport;
using namespace apache::thrift::protocol;

namespace {

    std::shared_ptr<cse::model_description> getModelDescription(FmuServiceClient &client, FmuId &fmuId) {
        ::fmuproxy::thrift::ModelDescription md = ModelDescription();
        client.getModelDescription(md, fmuId);
        return convert(md);
    }

}

cse::fmuproxy::remote_fmu::remote_fmu(FmuId fmuId, std::string host, unsigned int port): fmuId_(fmuId) {
    std::shared_ptr<TTransport> socket(new TSocket(host, port));
    transport_ = std::make_shared<TBufferedTransport>(socket);
    std::shared_ptr<TProtocol> protocol(new TBinaryProtocol(transport_));
    client_ = std::make_shared<FmuServiceClient>(protocol);
    transport_->open();
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
