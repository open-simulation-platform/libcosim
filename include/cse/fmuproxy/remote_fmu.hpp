
#ifndef CSE_REMOTE_FMU_HPP
#define CSE_REMOTE_FMU_HPP

#ifdef _WIN32
#include <winsock2.h>
#endif

#include <memory>
#include <string_view>

#include <thrift/transport/TSocket.h>
#include <thrift/protocol/TBinaryProtocol.h>
#include <thrift/transport/TTransportUtils.h>

#include <cse/model.hpp>
#include <cse/async_slave.hpp>
#include <cse/fmuproxy/fmu_service.hpp>

namespace cse {

    namespace fmuproxy {

        class remote_fmu {

        public:

            remote_fmu(std::string fmuId, std::string host, unsigned int port);

            std::shared_ptr<const cse::model_description> model_description() const;

            std::shared_ptr<cse::slave> instantiate_slave();

            ~remote_fmu() = default;

        private:

            std::string fmuId_;
            std::shared_ptr<const cse::model_description> modelDescription_;
            std::shared_ptr<::fmuproxy::thrift::FmuServiceClient> client_;
            std::shared_ptr<apache::thrift::transport::TTransport> transport_;

        };

    }

}

#endif
