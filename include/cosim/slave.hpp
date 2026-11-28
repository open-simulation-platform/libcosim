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

#include <cosim/exception.hpp>
#include <cosim/model_description.hpp>
#include <cosim/serialization.hpp>
#include <cosim/time.hpp>

#include <boost/container/vector.hpp>
#include <gsl/span>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>


namespace cosim
{


/**
 * Result of one FMI-style discrete-state update during a Clock event.
 */
struct clock_update_result
{
    bool discrete_states_need_update = false;
    bool terminate_simulation = false;
    std::optional<time_point> next_event_time;
};

enum class clock_interval_qualifier // NOLINT(performance-enum-size): Preserve the public ABI.
{
    not_yet_known,
    unchanged,
    changed,
};

struct clock_interval_update // NOLINT(cppcoreguidelines-pro-type-member-init): Preserve existing default-initialization behavior.
{
    clock_interval_qualifier qualifier;
    double interval = 0.0;
    std::optional<std::uint64_t> counter;
    std::optional<std::uint64_t> resolution;
};

struct event_update_result
{
    /// Another discrete-state update is required at the current time.
    bool discrete_states_need_update = false;
    /// The FMU requested that simulation stop immediately.
    bool terminate_simulation = false;
    /// The next scheduled event time, when specified by the FMU.
    std::optional<time_point> next_event_time;
};

/**
 * Optional control surface for a simulator with discrete Event Mode.
 *
 * An importer can coordinate input changes and repeated updates before
 * returning participants to Step Mode.
 */
class event_controller // NOLINT(cppcoreguidelines-special-member-functions): Preserve existing interface copy and move behavior.
{
public:
    virtual ~event_controller() = default;

    virtual bool is_in_event_mode() const noexcept = 0; // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.
    virtual void enter_event_mode() = 0;
    /// Commits one super-dense instant. An orchestrator can call this again
    /// before Step Mode if this participant or a connected peer needs another instant.
    virtual event_update_result update_event_state() = 0;
    virtual void enter_step_mode() = 0;
};


/**
 * Optional control surface for routing FMI Clock events.
 *
 * Clock values are scheduling events and are intentionally kept separate from
 * ordinary scalar and whole-variable transport.
 */
class clock_controller : public event_controller // NOLINT(cppcoreguidelines-special-member-functions): Preserve existing interface copy and move behavior.
{
public:
    ~clock_controller() override = default;

    /// Returns an input Clock schedule resolved during initialization, if any.
    virtual std::optional<clock_description> resolved_input_clock_schedule( // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.
        value_reference /*unused*/) const
    {
        return std::nullopt;
    }

    /// Reads an FMU-controlled input Clock interval in Initialization or Event Mode.
    virtual clock_interval_update get_input_clock_interval(value_reference /*unused*/)
    {
        throw error(make_error_code(errc::unsupported_feature),
            "This Clock controller does not provide runtime intervals");
    }

    /// Indicates whether a host-requested Clock interval needs Event Mode.
    virtual bool has_pending_clock_intervals() const { return false; } // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.

    /// Returns a queued interval without applying it to the FMU.
    virtual std::optional<clock_interval_update> pending_input_clock_interval( // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.
        value_reference /*unused*/) const
    {
        if (has_pending_clock_intervals()) {
            throw error(make_error_code(errc::unsupported_feature),
                "This Clock controller cannot inspect pending intervals");
        }
        return std::nullopt;
    }

    /// Applies pending host interval changes after entering Event Mode.
    virtual void apply_pending_clock_intervals() { }

    virtual bool get_output_clock(value_reference reference) = 0;
    /**
     * Reads Clock status without sampling associated values. The default uses
     * get_output_clock; controllers that sample values there must override it.
     */
    virtual bool probe_output_clock(value_reference reference)
    {
        return get_output_clock(reference);
    }
    virtual void set_input_clock(value_reference reference, bool active) = 0;
    virtual variable_value get_clocked_variable(
        variable_type type,
        value_reference reference) = 0;
    /**
     * Reads a Clock-associated value without applying output modifiers.
     * Modifier-aware controllers must override the default implementation.
     */
    virtual variable_value probe_clocked_variable(
        variable_type type, value_reference reference)
    {
        return get_clocked_variable(type, reference);
    }
    virtual void set_clocked_variable(
        variable_type type,
        value_reference reference,
        const variable_value& value) = 0;
    virtual clock_update_result update_clock_state() = 0;
    event_update_result update_event_state() override
    {
        const auto result = update_clock_state();
        return {
            result.discrete_states_need_update,
            result.terminate_simulation,
            result.next_event_time,
        };
    }
};


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
class slave // NOLINT(cppcoreguidelines-special-member-functions): Preserve existing interface copy and move behavior.
{
public:
    virtual ~slave() = default;

    /// Returns a model description.
    virtual cosim::model_description model_description() const = 0; // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.

    /**
     * Returns the optional Clock-routing capability.
     *
     * The default keeps legacy and non-clocked slaves unchanged.
     */
    virtual cosim::clock_controller* get_clock_controller() noexcept
    {
        return nullptr;
    }

    /// Returns the optional Event Mode capability.
    virtual cosim::event_controller* get_event_controller() noexcept
    {
        return nullptr;
    }

    /**
     * Configures local event iteration before simulation starts. The default
     * implementation does nothing for slaves without internal event loops.
     */
    virtual void set_max_event_iterations(std::size_t /*unused*/) { }

    /**
     * Whether an algorithm may automatically replay a step after restoring
     * state. Return false when callbacks can have non-replayable side effects.
     * This does not disable explicit state saving or restoration.
     */
    virtual bool supports_automatic_step_replay() const noexcept { return true; } // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.

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
     *  Performs a step and reports FMI-style endpoint and control flags.
     *
     *  Implementations with richer step semantics should override this
     *  function. The default preserves the legacy contract.
     */
    virtual step_result_info do_step_with_info(
        time_point currentT,
        duration deltaT)
    {
        const auto result = do_step(currentT, deltaT);
        return {
            result,
            currentT + deltaT,
            false,
            false,
            false,
            result == step_result::failed,
        };
    }

    /**
     *  Completes pending event handling after a step requested it.
     *
     *  Standalone callers can use this to finish an instance's local iteration.
     *  Built-in algorithms drive instances with an event controller through
     *  that controller instead. For instances without one, this is the
     *  fallback when the instance reports an event. The default is a no-op.
     */
    virtual void handle_events() { }

    /// Returns the outcome of the latest event iteration, if supported.
    virtual event_handling_status get_event_handling_status() const noexcept // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.
    {
        return {};
    }

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
     *  Retrieves complete scalar or array variable values.
     *
     *  The default implementation bridges scalar Real, Integer, Boolean, and
     *  String variables to the legacy typed methods. Implementations that
     *  support arrays or additional exact types must override this method.
     */
    virtual std::vector<variable_value> get_variable_values( // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.
        gsl::span<const typed_value_reference> variables) const;

    /**
     *  Sets complete scalar or array variable values.
     *
     *  The default implementation bridges scalar Real, Integer, Boolean, and
     *  String variables to the legacy typed methods. Before calling any typed
     *  setter, it checks the complete batch's counts, supported types, value
     *  types and scalar shapes and verifies scalar metadata for every typed
     *  reference in `model_description()`. Custom slaves using this bridge
     *  must describe those variables. Numeric bounds are left to typed setters.
     *  Overrides may provide different validation guarantees. A typed setter
     *  failure can leave earlier assignments applied; batch writes are not
     *  generally transactional.
     */
    virtual void set_variable_values(
        gsl::span<const typed_value_reference> variables,
        gsl::span<const variable_value> values);

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
        if (static_cast<size_t>(values->real.size()) != static_cast<size_t>(real_variables.size())) {
            values->real.resize(real_variables.size());
        }
        if (static_cast<size_t>(values->integer.size()) != static_cast<size_t>(integer_variables.size())) {
            values->integer.resize(integer_variables.size());
        }
        if (static_cast<size_t>(values->boolean.size()) != static_cast<size_t>(boolean_variables.size())) {
            values->boolean.resize(boolean_variables.size());
        }
        if (static_cast<size_t>(values->string.size()) != static_cast<size_t>(string_variables.size())) {
            values->string.resize(string_variables.size());
        }

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
    virtual serialization::node export_state(state_index stateIndex) const = 0; // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.

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
