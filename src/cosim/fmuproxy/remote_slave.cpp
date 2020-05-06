#include <cosim/error.hpp>
#include <cosim/fmuproxy/fmuproxy_helper.hpp>
#include <cosim/fmuproxy/remote_slave.hpp>
#include <utility>


cosim::fmuproxy::remote_slave::remote_slave(std::string instanceId,
    std::shared_ptr<cosim::fmuproxy::thrift_state> state,
    std::shared_ptr<const cosim::model_description> modelDescription)
    : terminated_(false)
    , instanceId_(std::move(instanceId))
    , state_(std::move(state))
    , modelDescription_(std::move(modelDescription))
{}

cosim::model_description cosim::fmuproxy::remote_slave::model_description() const
{
    return *modelDescription_;
}

void cosim::fmuproxy::remote_slave::setup(cosim::time_point startTime, std::optional<cosim::time_point> stopTime,
    std::optional<double> relativeTolerance)
{
    startTime_ = startTime;

    double start = to_double_time_point(startTime);
    double stop = to_double_time_point(stopTime.value_or(cosim::to_time_point(0)));
    double tolerance = relativeTolerance.value_or(0);

    ::fmuproxy::thrift::Status::type status;
    status = state_->client().setup_experiment(instanceId_, start, stop, tolerance);
    if (status != ::fmuproxy::thrift::Status::OK_STATUS) {
        CSE_PANIC();
    }
    status = state_->client().enter_initialization_mode(instanceId_);
    if (status != ::fmuproxy::thrift::Status::OK_STATUS) {
        CSE_PANIC();
    }
}

void cosim::fmuproxy::remote_slave::start_simulation()
{
    auto status = state_->client().exit_initialization_mode(instanceId_);
    if (status != ::fmuproxy::thrift::Status::OK_STATUS) {
        CSE_PANIC();
    }
}

void cosim::fmuproxy::remote_slave::end_simulation()
{
    if (!terminated_) {
        auto status = state_->client().terminate(instanceId_);
        if (status != ::fmuproxy::thrift::Status::OK_STATUS) {
            CSE_PANIC();
        }
        terminated_ = true;
    }
}

cosim::step_result cosim::fmuproxy::remote_slave::do_step(cosim::time_point, cosim::duration deltaT)
{

    double dt = to_double_duration(deltaT, startTime_);

    ::fmuproxy::thrift::StepResult result;
    state_->client().step(result, instanceId_, dt);
    if (result.status != ::fmuproxy::thrift::Status::OK_STATUS) {
        CSE_PANIC();
    }
    return cosim::step_result::complete;
}

void cosim::fmuproxy::remote_slave::get_real_variables(gsl::span<const cosim::value_reference> variables,
    gsl::span<double> values) const
{
    CSE_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) return;
    ::fmuproxy::thrift::RealRead read;
    ::fmuproxy::thrift::ValueReferences vr(variables.begin(), variables.end());
    state_->client().read_real(read, instanceId_, vr);
    if (read.status != ::fmuproxy::thrift::Status::OK_STATUS) {
        CSE_PANIC();
    }
    for (unsigned int i = 0; i < read.value.size(); i++) {
        values[i] = read.value[i];
    }
}

void cosim::fmuproxy::remote_slave::get_integer_variables(gsl::span<const cosim::value_reference> variables,
    gsl::span<int> values) const
{
    CSE_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) return;
    ::fmuproxy::thrift::IntegerRead read;
    ::fmuproxy::thrift::ValueReferences vr(variables.begin(), variables.end());
    state_->client().read_integer(read, instanceId_, vr);
    if (read.status != ::fmuproxy::thrift::Status::OK_STATUS) {
        CSE_PANIC();
    }
    for (unsigned int i = 0; i < read.value.size(); i++) {
        values[i] = read.value[i];
    }
}

void cosim::fmuproxy::remote_slave::get_boolean_variables(gsl::span<const cosim::value_reference> variables,
    gsl::span<bool> values) const
{
    CSE_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) return;
    ::fmuproxy::thrift::BooleanRead read;
    ::fmuproxy::thrift::ValueReferences vr(variables.begin(), variables.end());
    state_->client().read_boolean(read, instanceId_, vr);
    if (read.status != ::fmuproxy::thrift::Status::OK_STATUS) {
        CSE_PANIC();
    }
    for (unsigned int i = 0; i < read.value.size(); i++) {
        values[i] = read.value[i];
    }
}

void cosim::fmuproxy::remote_slave::get_string_variables(gsl::span<const cosim::value_reference> variables,
    gsl::span<std::string> values) const
{
    CSE_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) return;
    ::fmuproxy::thrift::StringRead read;
    ::fmuproxy::thrift::ValueReferences vr(variables.begin(), variables.end());
    state_->client().read_string(read, instanceId_, vr);
    for (unsigned int i = 0; i < read.value.size(); i++) {
        values[i] = read.value[i];
    }
}

void cosim::fmuproxy::remote_slave::set_real_variables(gsl::span<const cosim::value_reference> variables,
    gsl::span<const double> values)
{
    CSE_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) return;
    ::fmuproxy::thrift::ValueReferences vr(variables.begin(), variables.end());
    auto status = state_->client().write_real(instanceId_, vr, std::vector<double>(values.begin(), values.end()));
    if (status != ::fmuproxy::thrift::Status::OK_STATUS) {
        CSE_PANIC();
    }
}

void cosim::fmuproxy::remote_slave::set_integer_variables(gsl::span<const cosim::value_reference> variables,
    gsl::span<const int> values)
{
    CSE_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) return;
    ::fmuproxy::thrift::ValueReferences vr(variables.begin(), variables.end());
    auto status = state_->client().write_integer(instanceId_, vr, std::vector<int>(values.begin(), values.end()));
    if (status != ::fmuproxy::thrift::Status::OK_STATUS) {
        CSE_PANIC();
    }
}

void cosim::fmuproxy::remote_slave::set_boolean_variables(gsl::span<const cosim::value_reference> variables,
    gsl::span<const bool> values)
{
    CSE_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) return;
    ::fmuproxy::thrift::ValueReferences vr(variables.begin(), variables.end());
    auto status = state_->client().write_boolean(instanceId_, vr, std::vector<bool>(values.begin(), values.end()));
    if (status != ::fmuproxy::thrift::Status::OK_STATUS) {
        CSE_PANIC();
    }
}

void cosim::fmuproxy::remote_slave::set_string_variables(gsl::span<const cosim::value_reference> variables,
    gsl::span<const std::string> values)
{
    CSE_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) return;
    ::fmuproxy::thrift::ValueReferences vr(variables.begin(), variables.end());
    auto status = state_->client().write_string(instanceId_, vr, std::vector<std::string>(values.begin(), values.end()));
    if (status != ::fmuproxy::thrift::Status::OK_STATUS) {
        CSE_PANIC();
    }
}

cosim::fmuproxy::remote_slave::~remote_slave()
{
    end_simulation();
    state_->client().freeInstance(instanceId_);
}
