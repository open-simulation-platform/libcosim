#include <cse/fmuproxy/remote_fmu.hpp>

#include <error.hpp>
#include <fmuproxy/fmuproxy_helper.hpp>
#include <fmuproxy/remote_slave.hpp>

using namespace fmuproxy::thrift;
using namespace apache::thrift::transport;
using namespace apache::thrift::protocol;

cosim::fmuproxy::remote_fmu::remote_fmu(
    const FmuId& fmuId,
    std::shared_ptr<thrift_state> state)
    : fmuId_(fmuId)
    , state_(std::move(state))
{
    ::fmuproxy::thrift::ModelDescription md = ModelDescription();
    state_->client().get_model_description(md, fmuId);
    modelDescription_ = convert(md);
}

std::shared_ptr<const cosim::model_description> cosim::fmuproxy::remote_fmu::description() const noexcept
{
    return modelDescription_;
}

std::shared_ptr<cosim::slave> cosim::fmuproxy::remote_fmu::instantiate_slave()
{
    InstanceId instanceId;
    state_->client().create_instance(instanceId, fmuId_);
    return std::make_shared<cosim::fmuproxy::remote_slave>(instanceId, state_, modelDescription_);
}

std::shared_ptr<cosim::async_slave> cosim::fmuproxy::remote_fmu::instantiate(std::string_view)
{
    return cosim::make_background_thread_slave(instantiate_slave());
}
