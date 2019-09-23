/**
 *  \file
 *  Slave interface.
 */
#ifndef CSE_SLAVE_HPP
#define CSE_SLAVE_HPP

#include <cse/model.hpp>

#include <gsl/span>

#include <optional>
#include <string>


namespace cse
{


/**
 *  An interface for classes that represent co-simulation slaves.
 *
 *  The function call sequence is as follows:
 *
 *    1. `setup()`:
 *          Configure the slave and enter initialisation mode.
 *    2. `set_<type>_variables()`, `get_<type>_variables()`:
 *          Variable initialisation.  The functions may be called multiple times
 *          in any order.
 *    3. `start_simulation()`:
 *          End initialisation mode, start simulation.
 *    4. `do_step()`, `get_<type>_variables()`, `set_<type>_variables()`:
 *          Simulation.  The functions may be called multiple times, in this
 *          order.
 *    5. `end_simulation()`:
 *          End simulation.
 *
 *  Any method may throw an exception.  In almost all cases, the slave will
 *  be considered "broken" after this happens, and no further method calls
 *  will be made.  The sole exception to this rule is that the
 *  `set_<type>_variables()` methods may throw `nonfatal_bad_value` to
 *  indicate that one or more variables were out of the allowed range, but
 *  that the slave has either ignored or accepted these values and is able
 *  to proceed.
 */
class slave
{
public:
    virtual ~slave() {}

    /// Returns a model description.
    virtual cse::model_description model_description() const = 0;

    /**
     *  Instructs the slave to perform pre-simulation setup and enter
     *  initialisation mode.
     *
     *  This function is called when the slave has been added to an execution.
     *  The arguments `startTime` and `stopTime` represent the time interval
     *  inside which the slave's model equations are required to be valid.
     *  (In other words, it is guaranteed that do_step() will never be called
     *  with a time point outside this interval.)
     *
     *  \param [in] startTime
     *      The earliest possible time point for the simulation.
     *  \param [in] stopTime
     *      The latest possible time point for the simulation.  May be
     *      left unspecified if there is no defined stop time.
     *  \param [in] relativeTolerance
     *      If specified, this contains the relative tolerance of the step
     *      size controller.  The slave may then use this for error control
     *      in its internal integrator.
     */
    virtual void setup(
        time_point startTime,
        std::optional<time_point> stopTime,
        std::optional<double> relativeTolerance) = 0;

    /**
     *  Informs the slave that the initialisation stage ends and the
     *  simulation begins.
     */
    virtual void start_simulation() = 0;

    /// Informs the slave that the simulation run has ended.
    virtual void end_simulation() = 0;

    /**
     *  Performs model calculations for the time step which starts at
     *  the time point `currentT` and has a duration of `deltaT`.
     *
     *  If this is not the first time step, it can be assumed that the previous
     *  time step ended at `currentT`.  It can also be assumed that `currentT`
     *  is greater than or equal to the start time, and `currentT+deltaT` is
     *  less than or equal to the stop time, specified in the setup() call.
     *
     *  \returns
     *      Whether the step completed successfully or not.
     *      (Non-recoverable problems must be signaled with an exception.)
     *
     *  \note
     *      Currently, retrying a failed time step is not supported, but this is
     *      planned for a future version.
     */
    virtual step_result do_step(time_point currentT, duration deltaT) = 0;

    /**
     *  Retrieves the values of real variables.
     *
     *  On return, the `values` array will be filled with the values of the
     *  variables specified in `variables`, in the same order.
     *
     *  \pre `variables.size() == values.size()`
     */
    virtual void get_real_variables(
        gsl::span<const value_reference> variables,
        gsl::span<double> values) const = 0;

    /**
     *  Retrieves the values of integer variables.
     *
     *  On return, the `values` array will be filled with the values of the
     *  variables specified in `variables`, in the same order.
     *
     *  \pre `variables.size() == values.size()`
     */
    virtual void get_integer_variables(
        gsl::span<const value_reference> variables,
        gsl::span<int> values) const = 0;

    /**
     *  Retrieves the values of boolean variables.
     *
     *  On return, the `values` array will be filled with the values of the
     *  variables specified in `variables`, in the same order.
     *
     *  \pre `variables.size() == values.size()`
     */
    virtual void get_boolean_variables(
        gsl::span<const value_reference> variables,
        gsl::span<bool> values) const = 0;

    /**
     *  Retrieves the values of string variables.
     *
     *  On return, the `values` array will be filled with the values of the
     *  variables specified in `variables`, in the same order.
     *
     *  \pre `variables.size() == values.size()`
     */
    virtual void get_string_variables(
        gsl::span<const value_reference> variables,
        gsl::span<std::string> values) const = 0;

    /**
     *  Sets the values of real variables.
     *
     *  This will set the value of each variable specified in the `variables`
     *  array to the value given in the corresponding element of `values`.
     *
     *  The function may throw `nonfatal_bad_value` to indicate that one or
     *  more values were out of range or invalid, but that these values have
     *  been accepted or ignored so the simulation can proceed.
     *
     *  \pre `variables.size() == values.size()`
     */
    virtual void set_real_variables(
        gsl::span<const value_reference> variables,
        gsl::span<const double> values) = 0;

    /**
     *  Sets the values of integer variables.
     *
     *  This will set the value of each variable specified in the `variables`
     *  array to the value given in the corresponding element of `values`.
     *
     *  The function may throw `nonfatal_bad_value` to indicate that one or
     *  more values were out of range or invalid, but that these values have
     *  been accepted or ignored so the simulation can proceed.
     *
     *  \pre `variables.size() == values.size()`
     */
    virtual void set_integer_variables(
        gsl::span<const value_reference> variables,
        gsl::span<const int> values) = 0;

    /**
     *  Sets the values of boolean variables.
     *
     *  This will set the value of each variable specified in the `variables`
     *  array to the value given in the corresponding element of `values`.
     *
     *  The function may throw `nonfatal_bad_value` to indicate that one or
     *  more values were out of range or invalid, but that these values have
     *  been accepted or ignored so the simulation can proceed.
     *
     *  \pre `variables.size() == values.size()`
     */
    virtual void set_boolean_variables(
        gsl::span<const value_reference> variables,
        gsl::span<const bool> values) = 0;

    /**
     *  Sets the values of string variables.
     *
     *  This will set the value of each variable specified in the `variables`
     *  array to the value given in the corresponding element of `values`.
     *
     *  The function may throw `nonfatal_bad_value` to indicate that one or
     *  more values were out of range or invalid, but that these values have
     *  been accepted or ignored so the simulation can proceed.
     *
     *  \pre `variables.size() == values.size()`
     */
    virtual void set_string_variables(
        gsl::span<const value_reference> variables,
        gsl::span<const std::string> values) = 0;
};


} // namespace cse
#endif // header guard
