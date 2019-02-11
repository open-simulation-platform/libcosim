
#ifndef CSE_REMOTE_SLAVE_HPP
#define CSE_REMOTE_SLAVE_HPP

#include <string>

#include <cse/model.hpp>
#include <cse/async_slave.hpp>
#include <cse/fmuproxy/fmu_service.hpp>

namespace cse {

    namespace fmuproxy {

        class remote_slave : public slave {

        public:

            remote_slave(const std::string &instanceId,
                         std::shared_ptr<::fmuproxy::thrift::FmuServiceIf> client,
                         std::shared_ptr<const cse::model_description> modelDescription);


            cse::model_description model_description() const override;

            void setup(time_point startTime, std::optional<time_point> stopTime,
                       std::optional<double> relativeTolerance) override;

            void start_simulation() override;

            void end_simulation() override;

            cse::step_result do_step(time_point currentT, duration deltaT) override;

            void get_real_variables(gsl::span<const variable_index> variables, gsl::span<double> values) const override;

            void get_integer_variables(gsl::span<const variable_index> variables, gsl::span<int> values) const override;

            void
            get_boolean_variables(gsl::span<const variable_index> variables, gsl::span<bool> values) const override;

            void get_string_variables(gsl::span<const variable_index> variables,
                                      gsl::span<std::string> values) const override;

            void set_real_variables(gsl::span<const variable_index> variables, gsl::span<const double> values) override;

            void set_integer_variables(gsl::span<const variable_index> variables, gsl::span<const int> values) override;

            void
            set_boolean_variables(gsl::span<const variable_index> variables, gsl::span<const bool> values) override;

            void set_string_variables(gsl::span<const variable_index> variables,
                                      gsl::span<const std::string> values) override;

            ~remote_slave() override;

        private:

            bool terminated_;
            std::string instanceId_;
            cse::time_point startTime_;
            std::shared_ptr<::fmuproxy::thrift::FmuServiceIf> client_;
            std::shared_ptr<const cse::model_description> modelDescription_;

        };

    }

}

#endif
