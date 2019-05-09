#include <cse/fmuproxy/thrift_state.hpp>

#include <utility>

cse::fmuproxy::thrift_state::thrift_state(std::shared_ptr<::fmuproxy::thrift::fmu_service_if> client_,
    std::shared_ptr<apache::thrift::transport::TTransport> transport_)
    : client_(std::move(client_))
    , transport_(std::move(transport_))
{}


cse::fmuproxy::thrift_state::~thrift_state()
{
    if (transport_->isOpen()) {
        transport_->close();
    }
}
