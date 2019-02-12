
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


cse::fmuproxy::remote_fmu::remote_fmu(
        const FmuId &fmuId,
        std::shared_ptr<thrift_state> state) : fmuId_(fmuId), state_(std::move(state)) {
    ::fmuproxy::thrift::ModelDescription md = ModelDescription();
    state_->client_->getModelDescription(md, fmuId);
    modelDescription_ = convert(md);
}

std::shared_ptr<const cse::model_description> cse::fmuproxy::remote_fmu::model_description() const {
    return modelDescription_;
}

std::shared_ptr<cse::slave> cse::fmuproxy::remote_fmu::instantiate_slave() {
    InstanceId instanceId;
    state_->client_->createInstanceFromCS(instanceId, fmuId_);
    std::shared_ptr<cse::slave> slave(new cse::fmuproxy::remote_slave(instanceId, state_->client_, modelDescription_));
    return slave;
}
