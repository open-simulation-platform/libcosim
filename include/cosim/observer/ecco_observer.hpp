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

#include <cstddef>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>


namespace cosim
{

/// Aggregate statistics of a power bond's residual, accumulated over all observed samples.
struct power_bond_statistics
{
    std::size_t sample_count;
    double power_residual_min;
    double power_residual_max;
    double power_residual_mean;
    double power_residual_rms;
};

/**
 *  An observer that exposes the power bonds in a given `ecco_algorithm`, and their states, at each communication point.
 */
class ecco_observer : public observer
{
public:
    /**
     *  Constructor.
     *
     *  \param algorithm
     *      The ECCO algorithm whose power bond states are observed.
     *  \param bufferSize
     *      The number of most recent states retained per bond. Must be greater than zero.
     */
    explicit ecco_observer(std::shared_ptr<ecco_algorithm> algorithm, std::size_t bufferSize = 10000);

    std::unordered_map<std::string, power_bond_state> get_power_bond_states() const;

    power_bond_state get_current_power_bond_state(std::string_view name) const;

    std::vector<power_bond_state> get_power_bond_state_buffer(std::string_view name) const;

    power_bond_statistics get_power_bond_statistics(std::string_view name) const;

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
    std::size_t bufferSize_;
    std::unordered_map<std::string, power_bond_state> latestStates_;
    std::unordered_map<std::string, std::deque<power_bond_state>> history_;

    /// Note: this will aggregate over all observed samples, independent of the configured ring buffer size. 
    struct residual_accumulator
    {
        std::size_t count = 0;
        double sum = 0.0;
        double sum_squares = 0.0;
        double min = 0.0;
        double max = 0.0;
    };
    std::unordered_map<std::string, residual_accumulator> statistics_;
};


} // namespace cosim
#endif // header guard
