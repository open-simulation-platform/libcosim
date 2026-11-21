/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include <cosim/error.hpp>
#include <cosim/exception.hpp>
#include <cosim/model_description.hpp>
#include <cosim/proxy/remote_slave.hpp>
#include <cosim/serialization.hpp>
#include <cosim/slave.hpp>
#include <cosim/time.hpp>

#include <boost/range/algorithm/transform.hpp>
#include <gsl/span>
#include <proxyfmu/fmi/slave.hpp>
#include <proxyfmu/state.hpp>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>


namespace
{

void bad_status_throw(const std::string& method)
{
    const std::string reason("Bad status returned from remote slave during call to '" + method + "'.");
    throw cosim::error(make_error_code(cosim::errc::model_error), reason);
}

} // namespace

cosim::proxy::remote_slave::remote_slave(
    std::unique_ptr<proxyfmu::fmi::slave> slave,
    std::shared_ptr<const cosim::model_description> modelDescription)
    : slave_(std::move(slave))
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

    const double start = to_double_time_point(startTime);
    const double stop = to_double_time_point(stopTime.value_or(cosim::to_time_point(0)));
    const double tolerance = relativeTolerance.value_or(0);

    auto status = slave_->setup_experiment(start, stop, tolerance);
    if (!status) {
        bad_status_throw("setup_experiment");
    }
    status = slave_->enter_initialization_mode();
    if (!status) {
        bad_status_throw("enter_initialization_mode");
    }
}

void cosim::proxy::remote_slave::start_simulation()
{
    const auto status = slave_->exit_initialization_mode();
    if (!status) {
        bad_status_throw("exit_initialization_mode");
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
    const auto status = slave_->step(to_double_time_point(currentTime), to_double_duration(deltaT, startTime_));
    if (!status) {
        bad_status_throw("step");
    }
    return cosim::step_result::complete;
}

void cosim::proxy::remote_slave::get_real_variables(gsl::span<const cosim::value_reference> variables,
    gsl::span<double> values) const
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) {
        return;
    }

    const auto references = std::vector<proxyfmu::fmi::value_ref>(variables.begin(), variables.end());
    auto data = std::vector<double>(references.size());
    const auto status = slave_->get_real(references, data);
    if (!status) {
        bad_status_throw("get_real");
    }
    for (unsigned int i = 0; i < data.size(); i++) {
        values[i] = data.at(i); // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): GSL span indexing checks its bounds.
    }
}

void cosim::proxy::remote_slave::get_integer_variables(gsl::span<const cosim::value_reference> variables,
    gsl::span<int> values) const
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) {
        return;
    }

    const auto references = std::vector<proxyfmu::fmi::value_ref>(variables.begin(), variables.end());
    auto data = std::vector<int>(references.size());
    const auto status = slave_->get_integer(references, data);
    if (!status) {
        bad_status_throw("get_integer");
    }
    for (unsigned int i = 0; i < data.size(); i++) {
        values[i] = data.at(i); // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): GSL span indexing checks its bounds.
    }
}

void cosim::proxy::remote_slave::get_boolean_variables(gsl::span<const cosim::value_reference> variables,
    gsl::span<bool> values) const
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) {
        return;
    }

    const auto references = std::vector<proxyfmu::fmi::value_ref>(variables.begin(), variables.end());
    auto data = std::vector<bool>(references.size());
    const auto status = slave_->get_boolean(references, data);
    if (!status) {
        bad_status_throw("get_boolean");
    }
    for (unsigned int i = 0; i < data.size(); i++) {
        values[i] = data.at(i); // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): GSL span indexing checks its bounds.
    }
}

void cosim::proxy::remote_slave::get_string_variables(gsl::span<const cosim::value_reference> variables,
    gsl::span<std::string> values) const
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) {
        return;
    }

    const auto references = std::vector<proxyfmu::fmi::value_ref>(variables.begin(), variables.end());
    auto data = std::vector<std::string>(references.size());
    const auto status = slave_->get_string(references, data);
    if (!status) {
        bad_status_throw("get_string");
    }
    for (unsigned int i = 0; i < data.size(); i++) {
        values[i] = data.at(i); // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access): GSL span indexing checks its bounds.
    }
}

void cosim::proxy::remote_slave::set_real_variables(gsl::span<const cosim::value_reference> variables,
    gsl::span<const double> values)
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) {
        return;
    }

    const auto references = std::vector<proxyfmu::fmi::value_ref>(variables.begin(), variables.end());
    const auto _values = std::vector<double>(values.begin(), values.end());
    const auto status = slave_->set_real(references, _values);
    if (!status) {
        bad_status_throw("set_real");
    }
}

void cosim::proxy::remote_slave::set_integer_variables(gsl::span<const cosim::value_reference> variables,
    gsl::span<const int> values)
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) {
        return;
    }

    const auto references = std::vector<proxyfmu::fmi::value_ref>(variables.begin(), variables.end());
    const auto _values = std::vector<int>(values.begin(), values.end());
    const auto status = slave_->set_integer(references, _values);
    if (!status) {
        bad_status_throw("set_integer");
    }
}

void cosim::proxy::remote_slave::set_boolean_variables(gsl::span<const cosim::value_reference> variables,
    gsl::span<const bool> values)
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) {
        return;
    }

    const auto references = std::vector<proxyfmu::fmi::value_ref>(variables.begin(), variables.end());
    const auto _values = std::vector<bool>(values.begin(), values.end());
    const auto status = slave_->set_boolean(references, _values);
    if (!status) {
        bad_status_throw("set_boolean");
    }
}

void cosim::proxy::remote_slave::set_string_variables(gsl::span<const cosim::value_reference> variables,
    gsl::span<const std::string> values)
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    if (variables.empty()) {
        return;
    }

    const auto references = std::vector<proxyfmu::fmi::value_ref>(variables.begin(), variables.end());
    const auto _values = std::vector<std::string>(values.begin(), values.end());
    const auto status = slave_->set_string(references, _values);
    if (!status) {
        bad_status_throw("set_string");
    }
}

cosim::slave::state_index cosim::proxy::remote_slave::save_state()
{
    return slave_->save_state();
}

void cosim::proxy::remote_slave::save_state(state_index stateIndex)
{
    slave_->save_state(stateIndex);
}

void cosim::proxy::remote_slave::restore_state(state_index stateIndex)
{
    slave_->restore_state(stateIndex);
}

void cosim::proxy::remote_slave::release_state(state_index stateIndex)
{
    slave_->release_state(stateIndex);
}

cosim::serialization::node cosim::proxy::remote_slave::export_state(state_index stateIndex) const
{
    proxyfmu::state::exported_state state;
    slave_->export_state(stateIndex, state);

    // Create the exported state
    serialization::node exportedState;
    exportedState.put("scheme_version", state.schemeVersion);
    exportedState.put("fmu_uuid", state.uuid);

    std::vector<std::byte> vec;
    boost::range::transform(state.fmuState, std::back_inserter(vec),
        [](unsigned char byte) { return std::byte(byte); });

    exportedState.put("serialized_fmu_state", vec);
    exportedState.put("setup_complete", state.setupComplete);
    exportedState.put("simulation_started", state.simStarted);
    return exportedState;
}

cosim::slave::state_index cosim::proxy::remote_slave::import_state(
    const cosim::serialization::node& exportedState)
{
    proxyfmu::state::exported_state state;
    state.schemeVersion = exportedState.get<std::int32_t>("scheme_version");
    state.uuid = exportedState.get<std::string>("fmu_uuid");

    const auto& serializedFMUState = std::get<std::vector<std::byte>>(
        exportedState.get_child("serialized_fmu_state").data());
    state.fmuState.reserve(serializedFMUState.size());
    boost::range::transform(serializedFMUState, std::back_inserter(state.fmuState),
        [](std::byte byte) { return static_cast<unsigned char>(byte); });

    state.setupComplete = exportedState.get<bool>("setup_complete");
    state.simStarted = exportedState.get<bool>("simulation_started");
    return slave_->import_state(state);
}

cosim::proxy::remote_slave::~remote_slave()
{
    remote_slave::end_simulation();
    slave_->freeInstance();
}
