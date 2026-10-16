/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include <cosim/error.hpp>
#include <cosim/exception.hpp>
#include <cosim/proxy/remote_slave.hpp>

#include <proxyfmu/state.hpp>

#include <utility>
#include <vector>


namespace
{

void bad_status_throw(const std::string& method)
{
    std::string reason("Bad status returned from remote slave during call to '" + method + "'.");
    throw cosim::error(make_error_code(cosim::errc::model_error), reason);
}

template<typename T>
std::vector<T> to_vector(gsl::span<const T> span)
{
    return std::vector<T>(span.begin(), span.end());
}

proxyfmu::fmi::variable_refs to_variable_refs(const cosim::slave::variable_references& variables)
{
    return {
        to_vector(variables.integer),
        to_vector(variables.real),
        to_vector(variables.string),
        to_vector(variables.boolean)};
}

proxyfmu::fmi::variable_values to_variable_values(
    const cosim::slave::variable_references& variables,
    const cosim::slave::variable_values_view& values)
{
    COSIM_INPUT_CHECK(variables.real.size() == values.real.size());
    COSIM_INPUT_CHECK(variables.integer.size() == values.integer.size());
    COSIM_INPUT_CHECK(variables.boolean.size() == values.boolean.size());
    COSIM_INPUT_CHECK(variables.string.size() == values.string.size());
    return {
        to_vector(values.integer),
        to_vector(values.real),
        to_vector(values.string),
        to_vector(values.boolean)};
}

void copy_variable_values(
    const proxyfmu::fmi::variable_values& from,
    const proxyfmu::fmi::variable_refs& refs,
    cosim::slave::variable_values& to)
{
    if (from.reals.size() != refs.reals.size() ||
        from.integers.size() != refs.integers.size() ||
        from.booleans.size() != refs.booleans.size() ||
        from.strings.size() != refs.strings.size()) {
        throw cosim::error(make_error_code(cosim::errc::model_error),
            "Remote slave returned the wrong number of variable values");
    }
    to.real.assign(from.reals.begin(), from.reals.end());
    to.integer.assign(from.integers.begin(), from.integers.end());
    to.boolean.assign(from.booleans.begin(), from.booleans.end());
    to.string.assign(from.strings.begin(), from.strings.end());
}

} // namespace

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
        bad_status_throw("setup_experiment");
    }
    status = slave_->enter_initialization_mode();
    if (!status) {
        bad_status_throw("enter_initialization_mode");
    }
}

void cosim::proxy::remote_slave::start_simulation()
{
    auto status = slave_->exit_initialization_mode();
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
    auto status = slave_->step(to_double_time_point(currentTime), to_double_duration(deltaT, startTime_));
    if (!status) {
        bad_status_throw("step");
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
        bad_status_throw("get_real");
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
        bad_status_throw("get_integer");
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
        bad_status_throw("get_boolean");
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
        bad_status_throw("get_string");
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
        bad_status_throw("set_real");
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
        bad_status_throw("set_integer");
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
        bad_status_throw("set_boolean");
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
        bad_status_throw("set_string");
    }
}

void cosim::proxy::remote_slave::get_variables(
    const variable_references& variables,
    variable_values& values) const
{
    const auto refs = to_variable_refs(variables);
    proxyfmu::fmi::variable_values data;
    if (!refs.empty() && !slave_->get_all(refs, data)) {
        bad_status_throw("get_all");
    }
    copy_variable_values(data, refs, values);
}

void cosim::proxy::remote_slave::set_variables(
    const variable_references& variables,
    const variable_values_view& values)
{
    const auto data = to_variable_values(variables, values);
    const auto refs = to_variable_refs(variables);
    if (refs.empty()) return;
    if (!slave_->set_all(refs, data)) {
        bad_status_throw("set_all");
    }
}

cosim::step_result cosim::proxy::remote_slave::set_variables_do_step_get_variables(
    const variable_references& setVariables,
    const variable_values_view& setValues,
    time_point currentT,
    duration deltaT,
    const variable_references& getVariables,
    variable_values& getValues)
{
    const auto setData = to_variable_values(setVariables, setValues);
    const auto setRefs = to_variable_refs(setVariables);
    const auto getRefs = to_variable_refs(getVariables);
    proxyfmu::fmi::variable_values getData;
    const auto status = slave_->step_exchange(
        setRefs, setData,
        to_double_time_point(currentT), to_double_duration(deltaT, startTime_),
        getRefs, getData);
    if (!status) {
        bad_status_throw("step_exchange");
    }
    copy_variable_values(getData, getRefs, getValues);
    return cosim::step_result::complete;
}

cosim::slave::state_index cosim::proxy::remote_slave::save_state()
{
    return slave_->save_state();
}

void cosim::proxy::remote_slave::save_state(state_index idx)
{
    slave_->save_state(idx);
}

void cosim::proxy::remote_slave::restore_state(state_index idx)
{
    slave_->restore_state(idx);
}

void cosim::proxy::remote_slave::release_state(state_index idx)
{
    slave_->release_state(idx);
}

cosim::serialization::node cosim::proxy::remote_slave::export_state(state_index idx) const
{
    proxyfmu::state::exported_state state;
    slave_->export_state(idx, state);

    // Create the exported state
    serialization::node exportedState;
    exportedState.put("scheme_version", state.schemeVersion);
    exportedState.put("fmu_uuid", state.uuid);

    std::vector<std::byte> vec;
    std::transform(state.fmuState.begin(), state.fmuState.end(), std::back_inserter(vec),
        [](unsigned char c) { return std::byte(c); });

    exportedState.put("serialized_fmu_state", vec);
    exportedState.put("setup_complete", state.setupComplete);
    exportedState.put("simulation_started", state.simStarted);
    return exportedState;
}

cosim::slave::state_index cosim::proxy::remote_slave::import_state(
    const cosim::serialization::node& node)
{
    proxyfmu::state::exported_state state;
    state.schemeVersion = node.get<std::int32_t>("scheme_version");
    state.uuid = node.get<std::string>("fmu_uuid");

    const auto& serializedFMUState = std::get<std::vector<std::byte>>(
        node.get_child("serialized_fmu_state").data());
    state.fmuState.reserve(serializedFMUState.size());
    std::transform(serializedFMUState.begin(), serializedFMUState.end(), std::back_inserter(state.fmuState),
        [](std::byte b) { return static_cast<unsigned char>(b); });

    state.setupComplete = node.get<bool>("setup_complete");
    state.simStarted = node.get<bool>("simulation_started");
    return slave_->import_state(state);
}

cosim::proxy::remote_slave::~remote_slave()
{
    remote_slave::end_simulation();
    slave_->freeInstance();
}
