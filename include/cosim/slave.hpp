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
 *          order.  `set_variables_do_step_get_variables()` may be called
 *          instead, to do the setting, stepping and getting in one call.
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
 *
 *  Subclasses must implement all the pure virtual functions.  The batched
 *  functions `get_variables()`, `set_variables()` and
 *  `set_variables_do_step_get_variables()` have default implementations in
 *  terms of the per-type functions and `do_step()`, and only need to be
 *  overridden by slaves for which each call has a significant fixed cost,
 *  such as slaves in another process.  Such slaves should normally override
 *  all three, so that both initialisation and simulation steps benefit.
 *  Note that overriding the `get_variables()` and `set_variables()` virtual
 *  functions hides the convenience overloads of the same names in the
 *  subclass, unless they are brought back in with
 *  `using slave::get_variables;` and `using slave::set_variables;`.
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

    /**
     *  References to variables of every type, for the batched
     *  `get_variables()`, `set_variables()` and
     *  `set_variables_do_step_get_variables()` functions.
     *
     *  There is one field per variable type, so supporting a new type means
     *  adding a field here and in `variable_values` and `variable_values_view`
     *  rather than changing function signatures.
     */
    struct variable_references
    {
        /// Real variable references.
        gsl::span<const value_reference> real;

        /// Integer variable references.
        gsl::span<const value_reference> integer;

        /// Boolean variable references.
        gsl::span<const value_reference> boolean;

        /// String variable references.
        gsl::span<const value_reference> string;
    };

    /// Variable values of every type, as retrieved by `get_variables()`.
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

    /// Variable values of every type, as passed to `set_variables()`.
    struct variable_values_view
    {
        /// Real variable values.
        gsl::span<const double> real;

        /// Integer variable values.
        gsl::span<const int> integer;

        /// Boolean variable values.
        gsl::span<const bool> boolean;

        /// String variable values.
        gsl::span<const std::string> string;
    };

    /**
     *  Retrieves the values of variables of every type.
     *
     *  The default implementation calls `get_<type>_variables()` once per
     *  type.  Slaves for which each call carries a significant fixed cost,
     *  e.g. because it involves inter-process communication, may override
     *  it to retrieve everything in a single operation.
     *
     *  \param [in] variables
     *      The variables to retrieve, per type.  Any of the spans may be empty.
     *  \param [out] values
     *      On return, each vector will have been resized to the size of the
     *      corresponding span in `variables` and filled with the values of
     *      those variables, in the same order.
     *
     *  \throws
     *      Any exception thrown by `get_<type>_variables()`, or by an
     *      overriding implementation.  The slave is then considered broken,
     *      and the contents of `values` are unspecified.
     */
    virtual void get_variables(
        const variable_references& variables,
        variable_values& values) const
    {
        values.real.resize(variables.real.size());
        values.integer.resize(variables.integer.size());
        values.boolean.resize(variables.boolean.size());
        values.string.resize(variables.string.size());

        get_real_variables(variables.real, values.real);
        get_integer_variables(variables.integer, values.integer);
        get_boolean_variables(variables.boolean, values.boolean);
        get_string_variables(variables.string, values.string);
    }

    /**
     *  Sets the values of variables of every type.
     *
     *  The default implementation calls `set_<type>_variables()` once per
     *  type, in the order real, integer, boolean, string.  As with
     *  `get_variables()`, slaves may override it to set everything in a
     *  single operation.
     *
     *  \param [in] variables
     *      The variables to set, per type.  Any of the spans may be empty.
     *  \param [in] values
     *      The values to set, per type, in the same order as `variables`.
     *
     *  \throws nonfatal_bad_value
     *      If one or more values were out of range or invalid, but have been
     *      accepted or ignored so the simulation can proceed.  With the
     *      default implementation, the variable types after the one that
     *      threw have then not been set.
     *  \throws
     *      Any other exception thrown by `set_<type>_variables()`, or by an
     *      overriding implementation.  The slave is then considered broken.
     *
     *  \pre Each span in `values` has the same size as the corresponding
     *      span in `variables`.
     */
    virtual void set_variables(
        const variable_references& variables,
        const variable_values_view& values)
    {
        set_real_variables(variables.real, values.real);
        set_integer_variables(variables.integer, values.integer);
        set_boolean_variables(variables.boolean, values.boolean);
        set_string_variables(variables.string, values.string);
    }

    /**
     *  Sets variable values, performs a time step and retrieves variable
     *  values, in that order.
     *
     *  This is equivalent to calling `set_variables()`, `do_step()` and
     *  `get_variables()` in sequence, which is exactly what the default
     *  implementation does.  Slaves for which each call carries a significant
     *  fixed cost, e.g. because it involves inter-process communication, may
     *  override it to perform the whole sequence in a single operation.
     *
     *  \param [in] setVariables
     *      The variables to set before the step, per type.  Any of the spans
     *      may be empty.
     *  \param [in] setValues
     *      The values to set, per type, in the same order as `setVariables`.
     *  \param [in] currentT
     *      The start of the time step, as for `do_step()`.
     *  \param [in] deltaT
     *      The length of the time step, as for `do_step()`.
     *  \param [in] getVariables
     *      The variables to retrieve after the step, per type.  Any of the
     *      spans may be empty.
     *  \param [out] getValues
     *      On return, holds the values of `getVariables` as described for
     *      `get_variables()`.  The values are retrieved regardless of the step
     *      result, as with the separate calls.
     *
     *  \returns
     *      The result of the time step, as for `do_step()`.
     *
     *  \throws
     *      Any exception that `set_variables()`, `do_step()` or
     *      `get_variables()` may throw.  With the default implementation, a
     *      `nonfatal_bad_value` from `set_variables()` means that the step has
     *      not been performed.
     *
     *  \pre Each span in `setValues` has the same size as the corresponding
     *      span in `setVariables`, and the preconditions of `do_step()` hold.
     */
    virtual step_result set_variables_do_step_get_variables(
        const variable_references& setVariables,
        const variable_values_view& setValues,
        time_point currentT,
        duration deltaT,
        const variable_references& getVariables,
        variable_values& getValues)
    {
        set_variables(setVariables, setValues);
        const auto result = do_step(currentT, deltaT);
        get_variables(getVariables, getValues);
        return result;
    }

    /**
     *  Retrieves the values of real, integer, boolean and string variables.
     *
     *  Convenience overload which forwards to
     *  `get_variables(const variable_references&, variable_values&)`,
     *  and has the same semantics and exceptions.
     *
     *  \param [out] values
     *      On return, holds the variable values, as for the forwarded-to
     *      function.  Must not be null.
     *  \param [in] real_variables
     *      The real variables to retrieve.
     *  \param [in] integer_variables
     *      The integer variables to retrieve.
     *  \param [in] boolean_variables
     *      The boolean variables to retrieve.
     *  \param [in] string_variables
     *      The string variables to retrieve.
     */
    void get_variables(
        variable_values* values,
        gsl::span<const value_reference> real_variables,
        gsl::span<const value_reference> integer_variables,
        gsl::span<const value_reference> boolean_variables,
        gsl::span<const value_reference> string_variables) const
    {
        get_variables(
            variable_references{real_variables, integer_variables, boolean_variables, string_variables},
            *values);
    }

    /**
     *  Sets the values of real, integer, boolean and string variables.
     *
     *  Convenience overload which forwards to
     *  `set_variables(const variable_references&, const variable_values_view&)`,
     *  and has the same semantics, exceptions and preconditions.  Each
     *  `<type>_values` span holds the values for the variables in the
     *  corresponding `<type>_variables` span, in the same order.
     */
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
        set_variables(
            variable_references{real_variables, integer_variables, boolean_variables, string_variables},
            variable_values_view{real_values, integer_values, boolean_values, string_values});
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
