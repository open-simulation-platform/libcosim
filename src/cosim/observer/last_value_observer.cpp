/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/observer/last_value_observer.hpp"

#include "cosim/error.hpp"
#include "cosim/observer/slave_value_provider.hpp"

#include <map>
#include <mutex>


namespace cosim
{

last_value_observer::last_value_observer()
{ }

void last_value_observer::simulator_added(simulator_index index, observable* simulator, time_point /*currentTime*/)
{
    valueProviders_[index] = std::make_unique<slave_value_provider>(simulator);
}

void last_value_observer::simulator_removed(simulator_index index, time_point /*currentTime*/)
{
    valueProviders_.erase(index);
}

void last_value_observer::variables_connected(variable_id /*output*/, variable_id /*input*/, time_point /*currentTime*/)
{
}

void last_value_observer::variable_disconnected(variable_id /*input*/, time_point /*currentTime*/)
{
}

void last_value_observer::simulation_initialized(
    step_number /*firstStep*/,
    time_point /*startTime*/)
{
    for (const auto& entry : valueProviders_) {
        entry.second->observe();
    }
}

void last_value_observer::step_complete(
    step_number /*lastStep*/,
    duration /*lastStepSize*/,
    time_point /*currentTime*/)
{
}

void last_value_observer::simulator_step_complete(
    simulator_index index,
    step_number /*lastStep*/,
    duration /*lastStepSize*/,
    time_point /*currentTime*/)
{
    valueProviders_.at(index)->observe();
}

void last_value_observer::get_real(
    simulator_index sim,
    gsl::span<const value_reference> variables,
    gsl::span<double> values)
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    valueProviders_.at(sim)->get_real(variables, values);
}

void last_value_observer::get_integer(
    simulator_index sim,
    gsl::span<const value_reference> variables,
    gsl::span<int> values)
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    valueProviders_.at(sim)->get_int(variables, values);
}

void last_value_observer::get_boolean(
    simulator_index sim,
    gsl::span<const value_reference> variables,
    gsl::span<bool> values)
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    valueProviders_.at(sim)->get_boolean(variables, values);
}

void last_value_observer::get_string(
    simulator_index sim,
    gsl::span<const value_reference> variables,
    gsl::span<std::string> values)
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    valueProviders_.at(sim)->get_string(variables, values);
}

last_value_observer::~last_value_observer() noexcept = default;

} // namespace cosim
