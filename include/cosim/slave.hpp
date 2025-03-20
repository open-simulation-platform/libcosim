/**
 *  \file
 *  Slave interface.
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_SLAVE_HPP
#define COSIM_SLAVE_HPP

#include <cosim/model_description.hpp>
#include <cosim/serialization.hpp>
#include <cosim/time.hpp>

#include <boost/container/vector.hpp>
#include <gsl/span>

#include <optional>
#include <string>


namespace cosim
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
    virtual ~slave() = default;

    /// Returns a model description.
    virtual cosim::model_description model_description() const = 0;

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

    /// Result type for `get_variables()`.
    struct variable_values
    {
        /// Real variable values.
        std::vector<double> real;

        /// Integer variable values.
        std::vector<int> integer;

        /// Boolean variable values.
        boost::container::vector<bool> boolean;

        /// String variable values.
        std::vector<std::string> string;
    };

    void get_variables(
        variable_values* values,
        gsl::span<const value_reference> real_variables,
        gsl::span<const value_reference> integer_variables,
        gsl::span<const value_reference> boolean_variables,
        gsl::span<const value_reference> string_variables) const
    {
        if (static_cast<size_t>(values->real.size()) != static_cast<size_t>(real_variables.size()))
            values->real.resize(real_variables.size());
        if (static_cast<size_t>(values->integer.size()) != static_cast<size_t>(integer_variables.size()))
            values->integer.resize(integer_variables.size());
        if (static_cast<size_t>(values->boolean.size()) != static_cast<size_t>(boolean_variables.size()))
            values->boolean.resize(boolean_variables.size());
        if (static_cast<size_t>(values->string.size()) != static_cast<size_t>(string_variables.size()))
            values->string.resize(string_variables.size());

        get_real_variables(real_variables, values->real);
        get_integer_variables(integer_variables, values->integer);
        get_boolean_variables(boolean_variables, values->boolean);
        get_string_variables(string_variables, values->string);
    }

    void set_variables(
        gsl::span<const value_reference> real_variables,
        gsl::span<const double> real_values,
        gsl::span<const value_reference> integer_variables,
        gsl::span<const int> integer_values,
        gsl::span<const value_reference> boolean_variables,
        gsl::span<const bool> boolean_values,
        gsl::span<const value_reference> string_variables,
        gsl::span<const std::string> string_values)
    {

        set_real_variables(real_variables, real_values);
        set_integer_variables(integer_variables, integer_values);
        set_boolean_variables(boolean_variables, boolean_values);
        set_string_variables(string_variables, string_values);
    }

    /// A type used for references to saved states (see `save_state()`).
    using state_index = int;

    /**
     *  Saves the current state.
     *
     *  This will create and store a copy of the slave's current internal
     *  state, so that it can be restored at a later time.  The copy is stored
     *  internally in the slave, and must be referred to by the returned
     *  `state_index`. The index is only valid for this particular slave.
     *
     *  The function may be called at any point after `setup()` has been called.
     */
    virtual state_index save_state() = 0;

    /**
     *  Saves the current state, overwriting a previously-saved state.
     *
     *  This function does the same as `save_state()`, except that it
     *  overwrites a state which has previously been stored by that function.
     *  The old index thereafter refers to the newly-saved state.
     */
    virtual void save_state(state_index stateIndex) = 0;

    /**
     *  Restores a previously-saved state.
     *
     *  This restores the slave to a state which has previously been saved
     *  using `save_state()`.
     *
     *  Note that the saved state is supposed to be the *complete and exact*
     *  state of the slave at the moment `save_state()` was called. For example,
     *  if the state was saved while the slave was in initialisation mode
     *  (between `setup()` and `start_simulation()`), then it will be restored
     *  in that mode, and `start_simulation()` must be called before the
     *  simulation can start.  Similarly, if it is saved at logical time `t`,
     *  then the first `do_step()` call after restoration must start at `t`.
     */
    virtual void restore_state(state_index stateIndex) = 0;

    /**
     *  Frees all resources (e.g. memory) associated with a saved state.
     *
     *  After this, the state may no longer be restored with `restore_state()`,
     *  nor may it be overwritten with `save_state(state_index)`.  The
     *  implementation is free to reuse the same `state_index` at a later point.
     */
    virtual void release_state(state_index stateIndex) = 0;

    /**
     *  Exports a saved state.
     *
     *  This returns a previously-saved state in a generic format so it can be
     *  serialized, e.g. to write it to disk and use it in a later simulation.
     */
    virtual serialization::node export_state(state_index stateIndex) const = 0;

    /**
     *  Imports an exported state.
     *
     *  The imported state is added to the slave's internal list of saved
     *  states. Use `restore_state()` to restore it again. The state must have
     *  been saved by a slave of the same or a compatible type.
     */
    virtual state_index import_state(const serialization::node& exportedState) = 0;
};


} // namespace cosim
#endif // header guard
