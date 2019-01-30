
#include <cse/fmuproxy/fmuproxy_helper.hpp>
#include <cse/fmuproxy/remote_slave.hpp>

cse::fmuproxy::remote_slave::remote_slave(std::string instanceId,
                                          std::shared_ptr<::fmuproxy::thrift::FmuServiceClient> client,
                                          std::shared_ptr<const cse::model_description> modelDescription)
        : instanceId_(instanceId), client_(client), modelDescription_(modelDescription) {}

cse::model_description cse::fmuproxy::remote_slave::model_description() const {
    return *modelDescription_;
}

void cse::fmuproxy::remote_slave::setup(cse::time_point startTime, std::optional<cse::time_point> stopTime,
                                        std::optional<double> relativeTolerance) {

    startTime_ = startTime;

    double start = to_double_time_point(startTime);
    double stop = to_double_time_point(stopTime.value_or(cse::to_time_point(0)));
    double tolerance = relativeTolerance.value_or(0);

    client_->setupExperiment(instanceId_, start, stop, tolerance);
    client_->enterInitializationMode(instanceId_);

}

void cse::fmuproxy::remote_slave::start_simulation() {
    client_->exitInitializationMode(instanceId_);
}

void cse::fmuproxy::remote_slave::end_simulation() {
    client_->terminate(instanceId_);
}

cse::step_result cse::fmuproxy::remote_slave::do_step(cse::time_point, cse::duration deltaT) {

    double dt = to_double_duration(deltaT, startTime_);

    ::fmuproxy::thrift::StepResult result;
    client_->step(result, instanceId_, dt);

    return cse::step_result::complete;
}

void cse::fmuproxy::remote_slave::get_real_variables(gsl::span<const cse::variable_index> variables,
                                                     gsl::span<double> values) const {

}

void cse::fmuproxy::remote_slave::get_integer_variables(gsl::span<const cse::variable_index> variables,
                                                        gsl::span<int> values) const {

}

void cse::fmuproxy::remote_slave::get_boolean_variables(gsl::span<const cse::variable_index> variables,
                                                        gsl::span<bool> values) const {

}

void cse::fmuproxy::remote_slave::get_string_variables(gsl::span<const cse::variable_index> variables,
                                                       gsl::span<std::string> values) const {

}

void cse::fmuproxy::remote_slave::set_real_variables(gsl::span<const cse::variable_index> variables,
                                                     gsl::span<const double> values) {

}

void cse::fmuproxy::remote_slave::set_integer_variables(gsl::span<const cse::variable_index> variables,
                                                        gsl::span<const int> values) {

}

void cse::fmuproxy::remote_slave::set_boolean_variables(gsl::span<const cse::variable_index> variables,
                                                        gsl::span<const bool> values) {

}

void cse::fmuproxy::remote_slave::set_string_variables(gsl::span<const cse::variable_index> variables,
                                                       gsl::span<const std::string> values) {

}
