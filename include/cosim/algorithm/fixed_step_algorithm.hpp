/**
 *  \file
 *  Defines the class for a fixed step algorithm
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef LIBCOSIM_ALGORITHM_FIXED_STEP_ALGORITHM_HPP
#define LIBCOSIM_ALGORITHM_FIXED_STEP_ALGORITHM_HPP

#include <cosim/algorithm/algorithm.hpp>

namespace cosim
{

struct fixed_step_algorithm_params
{
    duration stepSize;
};

/**
 *  A fixed-stepsize co-simulation algorithm.
 *
 *  A simple implementation of `algorithm`. The simulation progresses
 *  at a fixed base stepsize. Simulators are stepped in parallel at an optional
 *  multiple of this base step size.
 */
class fixed_step_algorithm : public algorithm
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
    explicit fixed_step_algorithm(duration baseStepSize, std::optional<unsigned int> workerThreadCount = std::nullopt);
    explicit fixed_step_algorithm(fixed_step_algorithm_params params, std::optional<unsigned int> workerThreadCount = std::nullopt);

    ~fixed_step_algorithm() noexcept;

    fixed_step_algorithm(const fixed_step_algorithm&) = delete;
    fixed_step_algorithm& operator=(const fixed_step_algorithm&) = delete;

    fixed_step_algorithm(fixed_step_algorithm&&) noexcept;
    fixed_step_algorithm& operator=(fixed_step_algorithm&&) noexcept;

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
     * Sets step size decimation factor for a simulator.
     *
     * This will effectively set the simulator step size to a multiple
     * of the algorithm's base step size. The default decimation factor is 1.
     * Must be called *after* the simulator has been added to the algorithm with `add_simulator()`.
     *
     * \param simulator
     *      The index of the simulator.
     *
     * \param factor
     *      The stepsize decimation factor.
     */
    void set_stepsize_decimation_factor(simulator_index simulator, int factor);

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace cosim

#endif
