

#ifndef CSE_FMUPROXY_SHARED_THRIFT_STATE_HPP
#define CSE_FMUPROXY_SHARED_THRIFT_STATE_HPP

#include <memory>

#include <cse/fmuproxy/fmu_service.hpp>

namespace cse {

    namespace fmuproxy {

        class remote_fmu;
        class fmuproxy_client;

        class thrift_state {

            friend class remote_fmu;
            friend class fmuproxy_client;

        public:

            thrift_state(const std::shared_ptr<::fmuproxy::thrift::FmuServiceIf> &client_,
                         const std::shared_ptr<apache::thrift::transport::TTransport> &transport_);

            ~thrift_state();

        private:

            std::shared_ptr<::fmuproxy::thrift::FmuServiceIf> client_;
            std::shared_ptr<apache::thrift::transport::TTransport> transport_;

        };

    }

}

#endif
