
#include <cse/error.hpp>
#include <cse/fmuproxy/fmuproxy_helper.hpp>
#include <cse/fmuproxy/remote_slave.hpp>

cse::fmuproxy::remote_slave::remote_slave(std::string instanceId,
                                          std::shared_ptr<::fmuproxy::thrift::FmuServiceIf> client,
                                          std::shared_ptr<const cse::model_description> modelDescription)
        : terminated_(false), instanceId_(instanceId), client_(client), modelDescription_(modelDescription) {}

cse::model_description cse::fmuproxy::remote_slave::model_description() const {
    return *modelDescription_;
}

void cse::fmuproxy::remote_slave::setup(cse::time_point startTime, std::optional<cse::time_point> stopTime,
                                        std::optional<double> relativeTolerance) {

    startTime_ = startTime;

    double start = to_double_time_point(startTime);
    double stop = to_double_time_point(stopTime.value_or(cse::to_time_point(0)));
    double tolerance = relativeTolerance.value_or(0);

    ::fmuproxy::thrift::Status::type status;
    status = client_->setupExperiment(instanceId_, start, stop, tolerance);
    if (status != ::fmuproxy::thrift::Status::OK_STATUS) {
        CSE_PANIC();
    }
    status = client_->enterInitializationMode(instanceId_);
    if (status != ::fmuproxy::thrift::Status::OK_STATUS) {
        CSE_PANIC();
    }

}

void cse::fmuproxy::remote_slave::start_simulation() {
    auto status = client_->exitInitializationMode(instanceId_);
    if (status != ::fmuproxy::thrift::Status::OK_STATUS) {
        CSE_PANIC();
    }
}

void cse::fmuproxy::remote_slave::end_simulation() {
    if (!terminated_) {
        auto status = client_->terminate(instanceId_);
        if (status != ::fmuproxy::thrift::Status::OK_STATUS) {
            CSE_PANIC();
        }
        terminated_ = true;
    }
}

cse::step_result cse::fmuproxy::remote_slave::do_step(cse::time_point, cse::duration deltaT) {

    double dt = to_double_duration(deltaT, startTime_);

    ::fmuproxy::thrift::StepResult result;
    client_->step(result, instanceId_, dt);
    if (result.status != ::fmuproxy::thrift::Status::OK_STATUS) {
        CSE_PANIC();
    }
    return cse::step_result::complete;
}

void cse::fmuproxy::remote_slave::get_real_variables(gsl::span<const cse::variable_index> variables,
                                                     gsl::span<double> values) const {
    CSE_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) return;
    ::fmuproxy::thrift::RealRead read;
    ::fmuproxy::thrift::ValueReferences vr(variables.begin(), variables.end());
    client_->readReal(read, instanceId_, vr);
    if (read.status != ::fmuproxy::thrift::Status::OK_STATUS) {
        CSE_PANIC();
    }
    for (unsigned int i = 0; i < read.value.size(); i++) {
        values[i] = read.value[i];
    }
}

void cse::fmuproxy::remote_slave::get_integer_variables(gsl::span<const cse::variable_index> variables,
                                                        gsl::span<int> values) const {
    CSE_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) return;
    ::fmuproxy::thrift::IntegerRead read;
    ::fmuproxy::thrift::ValueReferences vr(variables.begin(), variables.end());
    client_->readInteger(read, instanceId_, vr);
    if (read.status != ::fmuproxy::thrift::Status::OK_STATUS) {
        CSE_PANIC();
    }
    for (unsigned int i = 0; i < read.value.size(); i++) {
        values[i] = read.value[i];
    }
}

void cse::fmuproxy::remote_slave::get_boolean_variables(gsl::span<const cse::variable_index> variables,
                                                        gsl::span<bool> values) const {
    CSE_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) return;
    ::fmuproxy::thrift::BooleanRead read;
    ::fmuproxy::thrift::ValueReferences vr(variables.begin(), variables.end());
    client_->readBoolean(read, instanceId_, vr);
    if (read.status != ::fmuproxy::thrift::Status::OK_STATUS) {
        CSE_PANIC();
    }
    for (unsigned int i = 0; i < read.value.size(); i++) {
        values[i] = read.value[i];
    }
}

void cse::fmuproxy::remote_slave::get_string_variables(gsl::span<const cse::variable_index> variables,
                                                       gsl::span<std::string> values) const {
    CSE_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) return;
    ::fmuproxy::thrift::StringRead read;
    ::fmuproxy::thrift::ValueReferences vr(variables.begin(), variables.end());
    client_->readString(read, instanceId_, vr);
    for (unsigned int i = 0; i < read.value.size(); i++) {
        values[i] = read.value[i];
    }
}

void cse::fmuproxy::remote_slave::set_real_variables(gsl::span<const cse::variable_index> variables,
                                                     gsl::span<const double> values) {
    CSE_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) return;
    ::fmuproxy::thrift::ValueReferences vr(variables.begin(), variables.end());
    auto status = client_->writeReal(instanceId_, vr, std::vector<double>(values.begin(), values.end()));
    if (status != ::fmuproxy::thrift::Status::OK_STATUS) {
        CSE_PANIC();
    }
}

void cse::fmuproxy::remote_slave::set_integer_variables(gsl::span<const cse::variable_index> variables,
                                                        gsl::span<const int> values) {
    CSE_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) return;
    ::fmuproxy::thrift::ValueReferences vr(variables.begin(), variables.end());
    auto status = client_->writeInteger(instanceId_, vr, std::vector<int>(values.begin(), values.end()));
    if (status != ::fmuproxy::thrift::Status::OK_STATUS) {
        CSE_PANIC();
    }
}

void cse::fmuproxy::remote_slave::set_boolean_variables(gsl::span<const cse::variable_index> variables,
                                                        gsl::span<const bool> values) {
    CSE_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) return;
    ::fmuproxy::thrift::ValueReferences vr(variables.begin(), variables.end());
    auto status = client_->writeBoolean(instanceId_, vr, std::vector<bool>(values.begin(), values.end()));
    if (status != ::fmuproxy::thrift::Status::OK_STATUS) {
        CSE_PANIC();
    }
}

void cse::fmuproxy::remote_slave::set_string_variables(gsl::span<const cse::variable_index> variables,
                                                       gsl::span<const std::string> values) {
    CSE_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) return;
    ::fmuproxy::thrift::ValueReferences vr(variables.begin(), variables.end());
    auto status = client_->writeString(instanceId_, vr, std::vector<std::string>(values.begin(), values.end()));
    if (status != ::fmuproxy::thrift::Status::OK_STATUS) {
        CSE_PANIC();
    }
}

cse::fmuproxy::remote_slave::~remote_slave() {
    end_simulation();
}
