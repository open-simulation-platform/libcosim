
#ifndef COSIM_FMUPROXY_FMUPROXYCLIENT_HPP
#define COSIM_FMUPROXY_FMUPROXYCLIENT_HPP

#include <cosim/fmuproxy/fmu_service.hpp>
#include <cosim/fmuproxy/remote_fmu.hpp>

#include <memory>

namespace cosim
{

namespace fmuproxy
{

class fmuproxy_client
{

public:
    fmuproxy_client(const std::string& host,
        unsigned int port,
        bool concurrent = false);

    std::shared_ptr<remote_fmu> from_url(const std::string& url);

    std::shared_ptr<remote_fmu> from_file(const std::string& file);

    std::shared_ptr<remote_fmu> from_guid(const std::string& guid);

private:
    std::shared_ptr<thrift_state> state_;
};

} // namespace fmuproxy

} // namespace cosim


#endif
