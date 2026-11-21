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
#include <cosim/event_iteration.hpp>

#include <cstddef>

namespace cosim
{

struct fixed_step_algorithm_params // NOLINT(cppcoreguidelines-pro-type-member-init): Preserve existing default-initialization behavior.
{
    duration stepSize;
    /// Limit for coupled rounds and local event iterations.
    std::size_t max_event_iterations = default_max_event_iterations;
    cosim::event_value_tolerance event_value_tolerance = default_event_value_tolerance;
};

/**
 *  A fixed-stepsize co-simulation algorithm.
 *
 *  A simple implementation of `algorithm`. The simulation progresses
 *  at a fixed base stepsize. Simulators are stepped in parallel at an optional
 *  multiple of this base step size.
 *  Event and Clock boundaries can shorten a step. With decimation, event-aware
 *  simulators are stepped first and may be restored and stepped again.
 *  After an early return or scheduled time event, the remaining interval is
 *  completed before normal base steps resume.
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

    ~fixed_step_algorithm() noexcept override;

    fixed_step_algorithm(const fixed_step_algorithm&) = delete;
    fixed_step_algorithm& operator=(const fixed_step_algorithm&) = delete;

    fixed_step_algorithm(fixed_step_algorithm&& /*other*/) noexcept;
    fixed_step_algorithm& operator=(fixed_step_algorithm&& /*other*/) noexcept;

    // `algorithm` methods
    void add_simulator(simulator_index index, simulator* sim, duration stepSizeHint) override;
    void remove_simulator(simulator_index index) override;
    void add_function(function_index index, function* fun) override;
    void connect_variables(variable_id output, variable_id input) override;
    void connect_variables(variable_id output, function_io_id input) override;
    void connect_variables(function_io_id output, variable_id input) override;
    void connect_variables(function_io_id output, function_io_id input) override;
    void disconnect_variable(variable_id input) override;
    void disconnect_variable(function_io_id input) override;
    void setup(time_point startTime, std::optional<time_point> stopTime) override;
    void initialize() override;
    std::pair<duration, std::unordered_set<simulator_index>> do_step(time_point currentT) override;
    algorithm::macro_step_result do_step_with_info(time_point currentT) override;
    serialization::node export_current_state() const override; // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.
    void import_state(const serialization::node& exportedState) override;

    /**
     * Sets step size decimation factor for a simulator.
     *
     * This will effectively set the simulator step size to a multiple
     * of the algorithm's base step size. The default decimation factor is 1.
     * Must be called *after* the simulator has been added to the algorithm with `add_simulator()`.
     * Clock-capable simulators require factor 1. Ordinary connections between
     * different factors transfer only on their common communication grid.
     *
     * \param simulator
     *      The index of the simulator.
     *
     * \param factor
     *      The stepsize decimation factor.
     */
    void set_stepsize_decimation_factor(simulator_index simulator, int factor);

    /**
     * Sets the positive event iteration limit before initialization.
     * Coupled exhaustion warns and defers remaining inputs; unfinished local
     * FMI event iteration fails rather than entering Step Mode.
     */
    void set_max_event_iterations(std::size_t limit);

    /// Configures coupled floating-point event comparisons before initialization.
    void set_event_value_tolerance(cosim::event_value_tolerance tolerance);
    /// Returns the configured coupled floating-point event tolerance.
    cosim::event_value_tolerance event_value_tolerance() const noexcept; // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.

    /// Returns the configured base communication step.
    duration base_step_size() const noexcept; // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.
    /// Returns the actual number of worker threads.
    std::size_t worker_thread_count() const noexcept; // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.
    /// Returns the current coupled event-iteration limit.
    std::size_t max_event_iterations() const noexcept; // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace cosim

#endif
