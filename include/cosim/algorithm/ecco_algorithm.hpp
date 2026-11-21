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
#include <cosim/event_iteration.hpp>

#include <cstddef>

namespace cosim
{

struct ecco_algorithm_params // NOLINT(cppcoreguidelines-pro-type-member-init): Preserve existing default-initialization behavior.
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
    /// Limit for coupled rounds and local event iterations.
    std::size_t max_event_iterations = default_max_event_iterations;
    cosim::event_value_tolerance event_value_tolerance = default_event_value_tolerance;
};

/**
 * Adaptive energy-conservation-based co-simulation algorithm.
 *
 * Power bonds provide the residual used by the step-size controller.
 * Event and Clock boundaries can shorten a communication step without
 * replacing the controller's next proposed step size. After an early return
 * or scheduled time event, the interrupted interval is completed before
 * normal adaptive steps resume; the remainder can be below the minimum.
 */
class ecco_algorithm : public algorithm
{
public:
    /**
     *  Constructor.
     *
     *  \param params
     *      Step-size limits and controller parameters.
     *
     *  \param workerThreadCount
     *      The number of worker threads to spawn for running FMUs
     */
    explicit ecco_algorithm(ecco_algorithm_params params, std::optional<unsigned int> workerThreadCount = std::nullopt);

    ~ecco_algorithm() noexcept override;

    ecco_algorithm(const ecco_algorithm&) = delete;
    ecco_algorithm& operator=(const ecco_algorithm&) = delete;

    ecco_algorithm(ecco_algorithm&& /*other*/) noexcept;
    ecco_algorithm& operator=(ecco_algorithm&& /*other*/) noexcept;

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
     * Sets the positive event iteration limit before initialization.
     * Coupled exhaustion warns and defers remaining inputs; unfinished local
     * FMI event iteration fails rather than entering Step Mode.
     */
    void set_max_event_iterations(std::size_t limit);

    /// Configures coupled floating-point event comparisons before initialization.
    void set_event_value_tolerance(cosim::event_value_tolerance tolerance);
    /// Returns the configured coupled floating-point event tolerance.
    cosim::event_value_tolerance event_value_tolerance() const noexcept; // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.

    /// Returns the configured initial nominal step, not the current adapted step.
    duration initial_step_size() const noexcept; // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.
    /// Returns the actual number of worker threads.
    std::size_t worker_thread_count() const noexcept; // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.
    /// Returns the current coupled event-iteration limit.
    std::size_t max_event_iterations() const noexcept; // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.

    /**
     * Adds two input/output variable pairs for power-residual calculation.
     * The four endpoints identify the two sides of one power bond.
     */
    void add_power_bond(cosim::variable_id input_a, cosim::variable_id output_a, cosim::variable_id input_b, cosim::variable_id output_b);

    /**
     * Retrieves the power-bond energies associated with a simulator index.
     */
    std::vector<double> get_powerbond_energies(cosim::simulator_index simulator_index);

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace cosim

#endif
