/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include <cosim/error.hpp>
#include <cosim/proxy/remote_slave.hpp>

#include <utility>


cosim::proxy::remote_slave::remote_slave(
    std::unique_ptr<proxyfmu::fmi::slave> slave,
    std::shared_ptr<const cosim::model_description> modelDescription)
    : terminated_(false)
    , slave_(std::move(slave))
    , modelDescription_(std::move(modelDescription))
{ }

cosim::model_description cosim::proxy::remote_slave::model_description() const
{
    return *modelDescription_;
}

void cosim::proxy::remote_slave::setup(cosim::time_point startTime, std::optional<cosim::time_point> stopTime,
    std::optional<double> relativeTolerance)
{
    startTime_ = startTime;

    double start = to_double_time_point(startTime);
    double stop = to_double_time_point(stopTime.value_or(cosim::to_time_point(0)));
    double tolerance = relativeTolerance.value_or(0);

    auto status = slave_->setup_experiment(start, stop, tolerance);
    if (!status) {
        COSIM_PANIC();
    }
    status = slave_->enter_initialization_mode();
    if (!status) {
        COSIM_PANIC();
    }
}

void cosim::proxy::remote_slave::start_simulation()
{
    auto status = slave_->exit_initialization_mode();
    if (!status) {
        COSIM_PANIC();
    }
}

void cosim::proxy::remote_slave::end_simulation()
{
    if (!terminated_) {
        slave_->terminate();
        terminated_ = true;
    }
}

cosim::step_result cosim::proxy::remote_slave::do_step(cosim::time_point currentTime, cosim::duration deltaT)
{
    auto status = slave_->step(to_double_time_point(currentTime), to_double_duration(deltaT, startTime_));
    if (!status) {
        COSIM_PANIC();
    }
    return cosim::step_result::complete;
}

void cosim::proxy::remote_slave::get_real_variables(gsl::span<const cosim::value_reference> variables,
    gsl::span<double> values) const
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) return;

    const auto vr = std::vector<proxyfmu::fmi::value_ref>(variables.begin(), variables.end());
    auto data = std::vector<double>(vr.size());
    auto status = slave_->get_real(vr, data);
    if (!status) {
        COSIM_PANIC();
    }
    for (unsigned int i = 0; i < data.size(); i++) {
        values[i] = data[i];
    }
}

void cosim::proxy::remote_slave::get_integer_variables(gsl::span<const cosim::value_reference> variables,
    gsl::span<int> values) const
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) return;

    const auto vr = std::vector<proxyfmu::fmi::value_ref>(variables.begin(), variables.end());
    auto data = std::vector<int>(vr.size());
    auto status = slave_->get_integer(vr, data);
    if (!status) {
        COSIM_PANIC();
    }
    for (unsigned int i = 0; i < data.size(); i++) {
        values[i] = data[i];
    }
}

void cosim::proxy::remote_slave::get_boolean_variables(gsl::span<const cosim::value_reference> variables,
    gsl::span<bool> values) const
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) return;

    const auto vr = std::vector<proxyfmu::fmi::value_ref>(variables.begin(), variables.end());
    auto data = std::vector<bool>(vr.size());
    auto status = slave_->get_boolean(vr, data);
    if (!status) {
        COSIM_PANIC();
    }
    for (unsigned int i = 0; i < data.size(); i++) {
        values[i] = data[i];
    }
}

void cosim::proxy::remote_slave::get_string_variables(gsl::span<const cosim::value_reference> variables,
    gsl::span<std::string> values) const
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) return;

    const auto vr = std::vector<proxyfmu::fmi::value_ref>(variables.begin(), variables.end());
    auto data = std::vector<std::string>(vr.size());
    auto status = slave_->get_string(vr, data);
    if (!status) {
        COSIM_PANIC();
    }
    for (unsigned int i = 0; i < data.size(); i++) {
        values[i] = data[i];
    }
}

void cosim::proxy::remote_slave::set_real_variables(gsl::span<const cosim::value_reference> variables,
    gsl::span<const double> values)
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) return;

    const auto vr = std::vector<proxyfmu::fmi::value_ref>(variables.begin(), variables.end());
    const auto _values = std::vector<double>(values.begin(), values.end());
    auto status = slave_->set_real(vr, _values);
    if (!status) {
        COSIM_PANIC();
    }
}

void cosim::proxy::remote_slave::set_integer_variables(gsl::span<const cosim::value_reference> variables,
    gsl::span<const int> values)
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) return;

    const auto vr = std::vector<proxyfmu::fmi::value_ref>(variables.begin(), variables.end());
    const auto _values = std::vector<int>(values.begin(), values.end());
    auto status = slave_->set_integer(vr, _values);
    if (!status) {
        COSIM_PANIC();
    }
}

void cosim::proxy::remote_slave::set_boolean_variables(gsl::span<const cosim::value_reference> variables,
    gsl::span<const bool> values)
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) return;

    const auto vr = std::vector<proxyfmu::fmi::value_ref>(variables.begin(), variables.end());
    const auto _values = std::vector<bool>(values.begin(), values.end());
    auto status = slave_->set_boolean(vr, _values);
    if (!status) {
        COSIM_PANIC();
    }
}

void cosim::proxy::remote_slave::set_string_variables(gsl::span<const cosim::value_reference> variables,
    gsl::span<const std::string> values)
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) return;

    const auto vr = std::vector<proxyfmu::fmi::value_ref>(variables.begin(), variables.end());
    const auto _values = std::vector<std::string>(values.begin(), values.end());
    auto status = slave_->set_string(vr, _values);
    if (!status) {
        COSIM_PANIC();
    }
}

cosim::proxy::remote_slave::~remote_slave()
{
    remote_slave::end_simulation();
    slave_->freeInstance();
}
