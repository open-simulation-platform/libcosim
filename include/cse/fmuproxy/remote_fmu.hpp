
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

            remote_fmu(std::string fmuId, std::string host, unsigned int port, bool concurrent = true);

            std::shared_ptr<const cse::model_description> model_description() const;

            std::shared_ptr<cse::slave> instantiate_slave();

            ~remote_fmu();

        private:

            std::string fmuId_;
            std::shared_ptr<const cse::model_description> modelDescription_;
            std::shared_ptr<::fmuproxy::thrift::FmuServiceIf> client_;
            std::shared_ptr<apache::thrift::transport::TTransport> transport_;

        };

    }

}

#endif
