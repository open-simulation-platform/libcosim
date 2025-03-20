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

    /**
     * Adds a variable pair for the power residual calculation.
     * \param uVec
     *     The index of the variable.
     */
    void add_power_bond(cosim::variable_id input_a, cosim::variable_id output_a, cosim::variable_id input_b, cosim::variable_id output_b);

    /**
     * Retrieves the energies in the power bond for the given simulator index.add_variable_value
     * \param simulator_index 
     *      The index of the simulator. 
     */
    std::vector<double> get_powerbond_energies(cosim::simulator_index simulator_index);

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace cosim

#endif
