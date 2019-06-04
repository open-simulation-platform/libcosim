
#include "cse/fmuproxy/fmuproxy_helper.hpp"
#include <cse/error.hpp>
#include <cse/fmuproxy/remote_fmu.hpp>
#include <cse/fmuproxy/remote_slave.hpp>

#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable : 4245)
    #include <thrift/protocol/TBinaryProtocol.h>
    #include <thrift/transport/TSocketPool.h>
    #include <thrift/transport/TTransportUtils.h>
    #pragma warning(pop)
#endif

using namespace fmuproxy::thrift;
using namespace apache::thrift::transport;
using namespace apache::thrift::protocol;

cse::fmuproxy::remote_fmu::remote_fmu(
    const FmuId& fmuId,
    std::shared_ptr<thrift_state> state)
    : fmuId_(fmuId)
    , state_(std::move(state))
{
    ::fmuproxy::thrift::ModelDescription md = ModelDescription();
    state_->client().get_model_description(md, fmuId);
    modelDescription_ = convert(md);
}

std::shared_ptr<const cse::model_description> cse::fmuproxy::remote_fmu::description() const noexcept
{
    return modelDescription_;
}

std::shared_ptr<cse::slave> cse::fmuproxy::remote_fmu::instantiate_slave()
{
    InstanceId instanceId;
    state_->client().create_instance_from_cs(instanceId, fmuId_);
    return std::make_shared<cse::fmuproxy::remote_slave>(instanceId, state_, modelDescription_);
}

std::shared_ptr<cse::async_slave> cse::fmuproxy::remote_fmu::instantiate(std::string_view)
{
    return cse::make_background_thread_slave(instantiate_slave());
}
