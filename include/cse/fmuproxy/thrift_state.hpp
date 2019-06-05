

#ifndef CSE_FMUPROXY_SHARED_THRIFT_STATE_HPP
#define CSE_FMUPROXY_SHARED_THRIFT_STATE_HPP

#include <cse/fmuproxy/fmu_service.hpp>

#include <memory>

namespace cse
{

namespace fmuproxy
{

class remote_fmu;
class fmuproxy_client;

class thrift_state
{

    friend class remote_fmu;
    friend class fmuproxy_client;

public:
    thrift_state(std::shared_ptr<::fmuproxy::thrift::fmu_service_if> client_,
        std::shared_ptr<apache::thrift::transport::TTransport> transport_);

    ::fmuproxy::thrift::fmu_service_if& client();

    apache::thrift::transport::TTransport& transport();

    ~thrift_state();

private:
    const std::shared_ptr<::fmuproxy::thrift::fmu_service_if> client_;
    const std::shared_ptr<apache::thrift::transport::TTransport> transport_;
};

} // namespace fmuproxy

} // namespace cse

#endif
