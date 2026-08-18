/**
 *  \file
 *  Defines an observer that exposes power bond states of an ECCO algorithm.
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_OBSERVER_ECCO_OBSERVER_HPP
#define COSIM_OBSERVER_ECCO_OBSERVER_HPP

#include <cosim/algorithm/ecco_algorithm.hpp>
#include <cosim/observer/observer.hpp>

#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>


namespace cosim
{

/**
 *  An observer that makes the power bond states of a running `ecco_algorithm`
 *  available at each communication point.
 *
 *  Consumers poll the latest states with `get_power_bond_state()` /
 *  `get_power_bond_states()`.
 */
class ecco_observer : public observer
{
public:
    explicit ecco_observer(std::shared_ptr<ecco_algorithm> algorithm);

    /// Returns the most recently observed states of all power bonds, keyed by name.
    std::unordered_map<std::string, power_bond_state> get_power_bond_states() const;

    /**
     * Returns the most recently observed state of the named power bond.
     * \throws std::out_of_range if no power bond with the given name exists.
     */
    power_bond_state get_power_bond_state(std::string_view name) const;

    void simulator_added(simulator_index, observable*, time_point) override;
    void simulator_removed(simulator_index, time_point) override;
    void variables_connected(variable_id output, variable_id input, time_point) override;
    void variable_disconnected(variable_id input, time_point) override;
    void simulation_initialized(step_number firstStep, time_point startTime) override;
    void step_complete(step_number lastStep, duration lastStepSize, time_point currentTime) override;
    void simulator_step_complete(simulator_index index, step_number lastStep, duration lastStepSize, time_point currentTime) override;
    void state_restored(step_number currentStep, time_point currentTime) override;

private:
    std::shared_ptr<ecco_algorithm> algorithm_;
    std::unordered_map<std::string, power_bond_state> latestStates_;
};


} // namespace cosim
#endif // header guard
