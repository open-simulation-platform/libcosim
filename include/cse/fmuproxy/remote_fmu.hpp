
#ifndef CSE_REMOTE_FMU_HPP
#define CSE_REMOTE_FMU_HPP

#ifdef _WIN32
//must be included before <windows.h>
#include <winsock2.h>
#endif

#include <string>
#include <memory>

#include <cse/model.hpp>
#include <cse/async_slave.hpp>
#include <cse/fmuproxy/fmu_service.hpp>

namespace cse {

    namespace fmuproxy {

        class remote_fmu {

        public:

            std::shared_ptr<const cse::model_description> model_description() const;

            std::shared_ptr<cse::slave> instantiate_slave();

            static remote_fmu from_url(const std::string &url, const std::string &host, unsigned int port, bool concurrent = false);

            static remote_fmu from_guid(const std::string &guid, const std::string &host, unsigned int port, bool concurrent = false);

            ~remote_fmu();


        private:

            std::string fmuId_;
            std::shared_ptr<const cse::model_description> modelDescription_;
            std::shared_ptr<::fmuproxy::thrift::FmuServiceIf> client_;
            std::shared_ptr<apache::thrift::transport::TTransport> transport_;

            remote_fmu(const ::fmuproxy::thrift::FmuId &fmuId, std::shared_ptr<apache::thrift::transport::TTransport> transport, std::shared_ptr<::fmuproxy::thrift::FmuServiceIf> client);

        };

    }

}

#endif
