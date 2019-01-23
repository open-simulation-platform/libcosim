/**
 *  \file
 *  \brief Defines the `time_series_provider` interface.
 */
#ifndef CSE_OBSERVER_TIME_SERIES_PROVIDER_HPP
#define CSE_OBSERVER_TIME_SERIES_PROVIDER_HPP

#include <gsl/span>

#include <cse/execution.hpp>
#include <cse/model.hpp>
#include <cse/observer/observer.hpp>


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
     * \param [in] variableIndex the variable index
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
        variable_index variableIndex,
        step_number fromStep,
        gsl::span<double> values,
        gsl::span<step_number> steps,
        gsl::span<time_point> times) = 0;

    /**
     * Retrieves a series of observed values, step numbers and times for an integer variable.
     *
     * \param [in] sim index of the simulator
     * \param [in] variableIndex the variable index
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
        variable_index variableIndex,
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
};


} // namespace cse
#endif // header guard
