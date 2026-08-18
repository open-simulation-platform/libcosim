/**
 *  \file
 *  Defines the class for a ECCO (Energy-Conservation-based Co-Simulation) algorithm
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef LIBCOSIM_ALGORITHM_ECCO_ALGORITHM_HPP
#define LIBCOSIM_ALGORITHM_ECCO_ALGORITHM_HPP

#include <cosim/algorithm/algorithm.hpp>

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cosim
{

struct ecco_algorithm_params
{
    double safety_factor;
    duration step_size;
    duration min_step_size;
    duration max_step_size;
    double min_change_rate;
    double max_change_rate;
    double abs_tolerance;
    double rel_tolerance;
    double p_gain;
    double i_gain;
};

/// Description of a power bond for runtime lookup.
struct power_bond_info
{
    std::string name;
    variable_id input_a;
    variable_id output_a;
    variable_id input_b;
    variable_id output_b;
};

/// Snapshot of a power bond's variable values and energies at a simulation step.
struct power_bond_state
{
    time_point time;
    double input_a_value;
    double output_a_value;
    double input_b_value;
    double output_b_value;

    /// Instantaneous power on each side of the bond [W].
    double power_a;
    double power_b;

    /// Absolute power residual across the bond, |power_a - power_b| [W].
    double power_residual;

    /// Energy accumulated on each side of the bond since the start [J].
    double energy_a;
    double energy_b;
};

/**
 *  A fixed-stepsize co-simulation algorithm.
 *
 *  A simple implementation of `algorithm`. The simulation progresses
 *  at a fixed base stepsize. Simulators are stepped in parallel at an optional
 *  multiple of this base step size.
 */
class ecco_algorithm : public algorithm
{
public:
    /**
     *  Constructor.
     *
     *  \param baseStepSize
     *      The base communication interval length.
     *
     *  \param workerThreadCount
     *      The number of worker threads to spawn for running FMUs
     */
    explicit ecco_algorithm(ecco_algorithm_params params, std::optional<unsigned int> workerThreadCount = std::nullopt);

    ~ecco_algorithm() noexcept;

    ecco_algorithm(const ecco_algorithm&) = delete;
    ecco_algorithm& operator=(const ecco_algorithm&) = delete;

    ecco_algorithm(ecco_algorithm&&) noexcept;
    ecco_algorithm& operator=(ecco_algorithm&&) noexcept;

    // `algorithm` methods
    void add_simulator(simulator_index i, simulator* s, duration stepSizeHint) override;
    void remove_simulator(simulator_index i) override;
    void add_function(function_index i, function* f) override;
    void connect_variables(variable_id output, variable_id input) override;
    void connect_variables(variable_id output, function_io_id input) override;
    void connect_variables(function_io_id output, variable_id input) override;
    void disconnect_variable(variable_id input) override;
    void disconnect_variable(function_io_id input) override;
    void setup(time_point startTime, std::optional<time_point> stopTime) override;
    void initialize() override;
    std::pair<duration, std::unordered_set<simulator_index>> do_step(time_point currentT) override;
    serialization::node export_current_state() const override;
    void import_state(const serialization::node& exportedState) override;

    /**
     * Adds a variable pair for the power residual calculation.
     * \param name
     *     The unique name identifying the power bond.
     */
    void add_power_bond(std::string name, cosim::variable_id input_a, cosim::variable_id output_a, cosim::variable_id input_b, cosim::variable_id output_b);

    /// Returns the names of all configured power bonds, in the order they were added.
    std::vector<std::string> get_power_bond_names() const;

    /**
     * Returns the information for the given power bond. 
     * \throws std::out_of_range if no power bond with the given name exists.
     */
    const power_bond_info& get_power_bond(std::string_view name) const;

    /**
     * Returns the most recently computed state (from the last completed step) of a given power bond.
     * \throws std::out_of_range if no power bond with the given name exists.
     */
    power_bond_state get_power_bond_state(std::string_view name) const;

    /// Returns the most recently computed states of all power bonds as an unordered map, keyed by the name.
    std::unordered_map<std::string, power_bond_state> get_power_bond_states() const;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace cosim

#endif
