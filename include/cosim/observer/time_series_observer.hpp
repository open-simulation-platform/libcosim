/**
 *  \file
 *  Observer-related functionality.
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_OBSERVER_TIME_SERIES_OBSERVER_HPP
#define COSIM_OBSERVER_TIME_SERIES_OBSERVER_HPP

#include <cosim/execution.hpp>
#include <cosim/model_description.hpp>
#include <cosim/observer/time_series_provider.hpp>
#include <cosim/time.hpp>

#include <memory>
#include <unordered_map>


namespace cosim
{


class slave_value_provider;


/**
 *  An observer implementation, storing all observed variable values for a user-specified set of variables in memory.
 */
class time_series_observer : public time_series_provider
{
public:
    /**
     * Default constructor. Creates a buffered `time_series_observer`, with a fixed buffer size of 10000 samples for each observed variable.
     */
    time_series_observer();

    /**
     * Constructor for a buffered `time_series_observer`, which will store up to `bufferSize` samples for each observed variable.
     */
    explicit time_series_observer(size_t bufferSize);

    void simulator_added(simulator_index, observable*, time_point) override;

    void simulator_removed(simulator_index, time_point) override;

    void variables_connected(variable_id output, variable_id input, time_point) override;

    void variable_disconnected(variable_id input, time_point) override;

    void simulation_initialized(
        step_number firstStep,
        time_point startTime) override;

    void step_complete(
        step_number lastStep,
        duration lastStepSize,
        time_point currentTime) override;

    void simulator_step_complete(
        simulator_index index,
        step_number lastStep,
        duration lastStepSize,
        time_point currentTime) override;

    /**
     * Start observing a variable.
     *
     * After calling this method, it will then be possible to extract observed values for this variable
     * with `get_real_samples()` or `get_integer_samples()`.
     */
    void start_observing(variable_id id);

    /**
     * Stop observing a variable.
     *
     * After calling this method, it will no longer be possible to extract observed values for this variable.
     */
    void stop_observing(variable_id id);

    std::size_t get_real_samples(
        simulator_index sim,
        value_reference valueReference,
        step_number fromStep,
        gsl::span<double> values,
        gsl::span<step_number> steps,
        gsl::span<time_point> times) override;

    std::size_t get_integer_samples(
        simulator_index sim,
        value_reference valueReference,
        step_number fromStep,
        gsl::span<int> values,
        gsl::span<step_number> steps,
        gsl::span<time_point> times) override;

    void get_step_numbers(
        simulator_index sim,
        duration duration,
        gsl::span<step_number> steps) override;

    void get_step_numbers(
        simulator_index sim,
        time_point tBegin,
        time_point tEnd,
        gsl::span<step_number> steps) override;

    std::size_t get_synchronized_real_series(
        simulator_index sim1,
        value_reference valueReference1,
        simulator_index sim2,
        value_reference valueReference2,
        step_number fromStep,
        gsl::span<double> values1,
        gsl::span<double> values2) override;

    ~time_series_observer() noexcept override;

private:
    class single_slave_observer;
    size_t bufSize_;
    std::unordered_map<simulator_index, std::unique_ptr<single_slave_observer>> slaveObservers_;
};

} // namespace cosim
#endif // header guard
