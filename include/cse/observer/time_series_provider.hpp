/**
 *  \file
 *  \brief Defines the `time_series_provider` interface.
 */
#ifndef CSE_OBSERVER_TIME_SERIES_PROVIDER_HPP
#define CSE_OBSERVER_TIME_SERIES_PROVIDER_HPP

#include <cse/execution.hpp>
#include <cse/model.hpp>
#include <cse/observer/observer.hpp>

#include <gsl/span>


namespace cse
{


/**
 *  An interface for time series providers.
 *
 *  The methods in this interface represent ways to extract data from an
 *  observer providing time series data.
 */
class time_series_provider : public observer
{
public:
    /**
     * Retrieves a series of observed values, step numbers and times for a real variable.
     *
     * \param [in] sim index of the simulator
     * \param [in] valueReference the value reference
     * \param [in] fromStep the step number to start from
     * \param [out] values the series of observed values
     * \param [out] steps the corresponding step numbers
     * \param [out] times the corresponding simulation times
     *
     * Returns the number of samples actually read, which may be smaller
     * than the sizes of `values` and `steps`.
     */
    virtual std::size_t get_real_samples(
        simulator_index sim,
        value_reference valueReference,
        step_number fromStep,
        gsl::span<double> values,
        gsl::span<step_number> steps,
        gsl::span<time_point> times) = 0;

    /**
     * Retrieves a series of observed values, step numbers and times for an integer variable.
     *
     * \param [in] sim index of the simulator
     * \param [in] valueReference the value reference
     * \param [in] fromStep the step number to start from
     * \param [out] values the series of observed values
     * \param [out] steps the corresponding step numbers
     * \param [out] times the corresponding simulation times
     *
     * Returns the number of samples actually read, which may be smaller
     * than the sizes of `values` and `steps`.
     */
    virtual std::size_t get_integer_samples(
        simulator_index sim,
        value_reference valueReference,
        step_number fromStep,
        gsl::span<int> values,
        gsl::span<step_number> steps,
        gsl::span<time_point> times) = 0;

    /**
     * Retrieves the step numbers for a range given by a duration.
     *
     * Helper function which can be used in conjunction with `get_xxx_samples()`
     * when it is desired to retrieve the latest available samples given a certain duration.
     *
     * \param [in] sim index of the simulator
     * \param [in] duration the duration to get step numbers for
     * \param [out] steps the corresponding step numbers
     */
    virtual void get_step_numbers(
        simulator_index sim,
        duration duration,
        gsl::span<step_number> steps) = 0;

    /**
     * Retrieves the step numbers for a range given by two points in time.
     *
     * Helper function which can be used in conjunction with `get_xxx_samples()`
     * when it is desired to retrieve samples between two points in time.
     *
     * \param [in] sim index of the simulator
     * \param [in] tBegin the start of the range
     * \param [in] tEnd the end of the range
     * \param [out] steps the corresponding step numbers
     */
    virtual void get_step_numbers(
        simulator_index sim,
        time_point tBegin,
        time_point tEnd,
        gsl::span<step_number> steps) = 0;

    /**
     * Retrieves two time-synchronized series of observed values for two real variables.
     *
     * \param [in] sim1 index of the first simulator
     * \param [in] valueReference1 the first value reference
     * \param [in] sim2 index of the second simulator
     * \param [in] valueReference2 the second value reference
     * \param [in] fromStep the step number to start from
     * \param [out] values1 the first series of observed values
     * \param [out] values2 the second series of observed values
     *
     * Returns the number of samples actually read, which may be smaller
     * than the sizes of `values1` and `values2`.
     */
    virtual std::size_t get_synchronized_real_series(
        simulator_index sim1,
        value_reference valueReference1,
        simulator_index sim2,
        value_reference valueReference2,
        step_number fromStep,
        gsl::span<double> values1,
        gsl::span<double> values2) = 0;
};


} // namespace cse
#endif // header guard
