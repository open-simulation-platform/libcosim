
#ifndef CSE_FMUPROXY_REMOTE_FMU_HPP
#define CSE_FMUPROXY_REMOTE_FMU_HPP

#ifdef _WIN32
//must be included before <windows.h>
#    include <winsock2.h>
#endif

#include <cse/async_slave.hpp>
#include <cse/fmuproxy/fmu_service.hpp>
#include <cse/fmuproxy/thrift_state.hpp>
#include <cse/model.hpp>

#include <memory>
#include <string>

namespace cse
{

namespace fmuproxy
{

class fmuproxy_client;

class remote_fmu
{

    friend class fmuproxy_client;

public:
    std::shared_ptr<const cse::model_description> model_description() const;

    std::shared_ptr<cse::slave> instantiate_slave();


private:
    const std::string fmuId_;
    std::shared_ptr<thrift_state> state_;
    std::shared_ptr<const cse::model_description> modelDescription_;

    remote_fmu(const ::fmuproxy::thrift::FmuId& fmuId,
        std::shared_ptr<thrift_state> state);
};

} // namespace fmuproxy

} // namespace cse

#endif
