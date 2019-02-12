
#include <cse/fmuproxy/thrift_state.hpp>

cse::fmuproxy::thrift_state::thrift_state(const std::shared_ptr<::fmuproxy::thrift::FmuServiceIf> &client_,
                                          const std::shared_ptr<apache::thrift::transport::TTransport> &transport_)
        : client_(client_), transport_(transport_) {}


cse::fmuproxy::thrift_state::~thrift_state() {
    if (transport_->isOpen()) {
        transport_->close();
    }
}

