#include <cse/fmuproxy/thrift_state.hpp>

#include <utility>

cosim::fmuproxy::thrift_state::thrift_state(std::shared_ptr<::fmuproxy::thrift::fmu_service_if> client_,
    std::shared_ptr<apache::thrift::transport::TTransport> transport_)
    : client_(std::move(client_))
    , transport_(std::move(transport_))
{}

::fmuproxy::thrift::fmu_service_if& cosim::fmuproxy::thrift_state::client()
{
    return *client_;
}
apache::thrift::transport::TTransport& cosim::fmuproxy::thrift_state::transport()
{
    return *transport_;
}

cosim::fmuproxy::thrift_state::~thrift_state()
{
    if (transport_->isOpen()) {
        transport_->close();
    }
}
