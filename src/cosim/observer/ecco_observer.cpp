/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/observer/ecco_observer.hpp"

#include <stdexcept>
#include <utility>


namespace cosim
{

ecco_observer::ecco_observer(std::shared_ptr<ecco_algorithm> algorithm, std::size_t bufferSize)
    : algorithm_(std::move(algorithm))
    , bufferSize_(bufferSize)
{
    if (algorithm_ == nullptr) {
        throw std::invalid_argument("ecco_observer requires a valid ecco_algorithm");
    }
    if (bufferSize_ == 0) {
        throw std::invalid_argument("ecco_observer requires a buffer size greater than zero");
    }
}

std::unordered_map<std::string, power_bond_state> ecco_observer::get_power_bond_states() const
{
    return latestStates_;
}

power_bond_state ecco_observer::get_current_power_bond_state(std::string_view name) const
{
    const auto it = latestStates_.find(std::string(name));
    if (it == latestStates_.end()) {
        throw std::out_of_range("No power bond named '" + std::string(name) + "'");
    }
    return it->second;
}

std::vector<power_bond_state> ecco_observer::get_power_bond_state_buffer(std::string_view name) const
{
    const auto it = history_.find(std::string(name));
    if (it == history_.end()) {
        throw std::out_of_range("No samples recorded for power bond '" + std::string(name) + "'");
    }
    return {it->second.begin(), it->second.end()};
}

void ecco_observer::simulator_added(simulator_index, observable*, time_point) { }

void ecco_observer::simulator_removed(simulator_index, time_point) { }

void ecco_observer::variables_connected(variable_id, variable_id, time_point) { }

void ecco_observer::variable_disconnected(variable_id, time_point) { }

void ecco_observer::simulation_initialized(step_number, time_point) { }

void ecco_observer::step_complete(step_number, duration, time_point)
{
    latestStates_ = algorithm_->get_power_bond_states();
    for (const auto& [name, state] : latestStates_) {
        auto& buffer = history_[name];
        buffer.push_back(state);
        if (buffer.size() > bufferSize_) {
            buffer.pop_front();
        }
    }
}

void ecco_observer::simulator_step_complete(simulator_index, step_number, duration, time_point) { }

void ecco_observer::state_restored(step_number, time_point) { }

} // namespace cosim
