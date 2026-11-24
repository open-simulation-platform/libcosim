/**
 *  \file
 *  Defines the simulator interface
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef LIBCOSIM_ALGORITHM_SIMULATOR_HPP
#define LIBCOSIM_ALGORITHM_SIMULATOR_HPP

#include <cosim/manipulator/manipulator.hpp>
#include <cosim/model_description.hpp>
#include <cosim/serialization.hpp>
#include <cosim/time.hpp>

#include <cstddef>
#include <optional>
#include <string_view>
#include <unordered_set>

namespace cosim
{

class clock_controller;
class event_controller;

/**
 *  A simulator interface for co-simulation algorithms.
 *
 *  This is the simulator interface exposed to `algorithm` implementers,
 *  and is used to control one "sub-simulator" in a co-simulation.
 */
class simulator : public manipulable
{
public:
    /**
     * Returns the optional Clock-routing capability.
     *
     * The default keeps existing simulator implementations source-compatible.
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
     * Configures local event iteration before setup. Implementations without
     * internally managed event iteration may ignore the limit.
     */
    virtual void set_max_event_iterations(std::size_t /*unused*/) { }

    /**
     * Stages input values and reports whether they require Event Mode.
     * Implementations that return true must support apply_event_inputs().
     * When `evaluateUnstagedModifiers` is false, modifiers for inputs that
     * have not been changed must not be evaluated for a subsequent step.
     */
    virtual bool prepare_input_events(
        duration /*unused*/,
        bool evaluateUnstagedModifiers = true);

    /**
     * Evaluates the staged inputs' modifiers again for a step size that
     * input events changed after `prepare_input_events()`.
     *
     * Returns true if any modifier was evaluated; the caller then prepares the
     * inputs again, since the new values can cause new input events. Every
     * input modifier must be replay-safe. The default does not support this
     * and throws when replay is required.
     * \param deltaT Duration of the shortened step.
     * \return Whether any input modifier was reevaluated.
     */
    virtual bool reevaluate_input_modifiers(duration deltaT);

    /// Makes deferred events eligible only after their original physical time.
    virtual void begin_input_event_coordination(time_point /*unused*/) { }

    /// Defers staged input events without applying them in Step Mode.
    virtual void defer_input_events(time_point /*unused*/);

    /// Applies staged inputs while the simulator is in Event Mode.
    virtual void apply_event_inputs();

    /// Applies eligible inputs, leaving inputs that cannot be set again staged.
    virtual void apply_coupled_event_inputs() { apply_event_inputs(); }

    /// Whether a prepared event can still be applied at the current instant.
    virtual bool has_applicable_event_inputs() const { return true; } // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.

    /// Releases unconsumed staging after event coordination without taking a step.
    virtual void finish_input_event_coordination();

    /// Refreshes ordinary observable values after Event Mode updates.
    virtual void refresh_event_values();

    /// Returns the unmodified value used to detect an event-driven output change.
    virtual variable_value get_event_output_value( // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.
        variable_type type, value_reference reference) const
    {
        return get_variable_value(type, reference);
    }

    /**
     * Stages a Clock-associated input without accessing the FMU or running
     * its modifier. The value is applied when its input Clock is activated.
     * Custom simulators must override this operation to support clocked data
     * routing; the default reports the feature as unsupported.
     */
    virtual void stage_clocked_input(
        variable_type type, value_reference reference, const variable_value& value);

    /**
     *  Sets the value of a real variable.
     *
     *  The variable must previously have been exposed with `expose_for_setting()`.
     */
    virtual void set_real(value_reference reference, double value) = 0;

    /**
     *  Sets the value of an integer variable.
     *
     *  The variable must previously have been exposed with `expose_for_setting()`.
     */
    virtual void set_integer(value_reference reference, int value) = 0;

    /**
     *  Sets the value of a boolean variable.
     *
     *  The variable must previously have been exposed with `expose_for_setting()`.
     */
    virtual void set_boolean(value_reference reference, bool value) = 0;

    /**
     *  Sets the value of a string variable.
     *
     *  The variable must previously have been exposed with `expose_for_setting()`.
     */
    virtual void set_string(value_reference reference, std::string_view value) = 0;

    /**
     *  Sets an owning scalar or array value.
     *
     *  The default implementation bridges the four legacy scalar types.
     */
    virtual void set_variable_value(
        variable_type type,
        value_reference reference,
        const variable_value& value);

    /**
     *  Performs pre-simulation setup, instantiates the FMUs and enters
     *  initialisation mode.
     *
     *  This function must be called exactly once, before initialisation and
     *  simulation can begin (i.e. before the first time either of
     *  `do_iteration()` or `do_step()` are called).
     *
     *  \param startTime
     *      The logical time at which the simulation will start. (The first
     *      time `do_step()` is called, its `currentT` argument must have
     *      the same value.)
     *  \param stopTime
     *      The logical time at which the simulation will end. If specified,
     *      the simulator may not be stepped beyond this point.
     *  \param relativeTolerance
     *      A suggested relative tolerance for the internal error estimation
     *      of the simulator's solver.  This should be specified if the
     *      co-simulation algorithm itself uses error estimation to determine
     *      the length of communication intervals.  If the simulator's
     *      solver doesn't use error estimation, it will just ignore this
     *      parameter.
     */
    virtual void setup(
        time_point startTime,
        std::optional<time_point> stopTime,
        std::optional<double> relativeTolerance) = 0;

    /// Returns all value references of real type that currently have an active modifier.
    virtual const std::unordered_set<value_reference>& get_modified_real_variables() const = 0; // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.

    /// Returns all value references of integer type that currently have an active modifier.
    virtual const std::unordered_set<value_reference>& get_modified_integer_variables() const = 0; // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.

    /// Returns all value references of boolean type that currently have an active modifier.
    virtual const std::unordered_set<value_reference>& get_modified_boolean_variables() const = 0; // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.

    /// Returns all value references of string type that currently have an active modifier.
    virtual const std::unordered_set<value_reference>& get_modified_string_variables() const = 0; // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.

    /// Whether this simulator has active input or output modifiers.
    virtual bool has_variable_modifiers() const // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.
    {
        return !get_modified_real_variables().empty() ||
            !get_modified_integer_variables().empty() ||
            !get_modified_boolean_variables().empty() ||
            !get_modified_string_variables().empty();
    }

    /**
     * Whether automatic step replay is safe in principle. A checkpoint may
     * still be unavailable because of pending inputs or FMU state.
     */
    virtual bool supports_automatic_step_replay() const // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.
    {
        return !has_variable_modifiers();
    }

    /**
     *  Applies the inputs staged for the next step so that the state can be
     *  saved immediately before it, and returns true on success.
     *
     *  The following `do_step()` call then has nothing left to apply. Nothing
     *  is applied and false is returned when the staged inputs cannot be
     *  applied separately or automatic replay is unsafe, for example while
     *  variable modifiers or non-replayable callbacks are active.
     *  The default does not support this.
     * \param deltaT Duration of the step for which the inputs were staged.
     * \return True if the pending ordinary inputs were applied.
     */
    virtual bool apply_pending_step_inputs(duration /*deltaT*/)
    {
        return false;
    }

    /**
     *  Updates the simulator with new input values and makes it calculate
     *  new output values, without advancing logical time.
     *
     *  This function can be used in the initialisation phase, after `setup()`
     *  has been called and before the call to `start_simulation()`. It enables
     *  iterative initialisation of the system. The purpose could be to
     *  propagate initial values between simulators and/or bring the system
     *  to a steady state.
     */
    virtual void do_iteration() = 0;

    /**
     *  Signals to the simulator that the initialization phase is complete
     *  and that stepping will begin.
     *
     *  This function must be called at the end of the initialisation phase,
     *  after any call to `do_iteration()` and before the first `do_step()` call.
     */
    virtual void start_simulation() = 0;

    /**
     *  Performs a single time step.
     *
     *  This causes the simulator to perform its computations for the logical
     *  time interval from `currentT` to `currentT+deltaT`.
     *
     *  \param currentT
     *      The starting point of the time step.
     *  \param deltaT
     *      The length of the time step. Must be positive.
     */
    virtual step_result do_step(
        time_point currentT,
        duration deltaT) = 0;

    /// Performs a step and reports the internal time and control flags.
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

    /// Completes pending event handling requested by the last step.
    virtual void handle_events() { }

    /// Returns the outcome of the latest event iteration, if supported.
    virtual event_handling_status get_event_handling_status() const noexcept // NOLINT(modernize-use-nodiscard): Preserve public source compatibility.
    {
        return {};
    }

    /// A type used for references to saved states (see `save_state()`).
    using state_index = int;

    /**
     *  Saves the current state.
     *
     *  This will create and store a copy of the simulator's current internal
     *  state, so that it can be restored at a later time.  The copy is stored
     *  internally in the simulator, and must be referred to by the returned
     *  `state_index`. The index is only valid for this particular simulator.
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
     *  This restores the simulator to a state which has previously been saved
     *  using `save_state()`.
     *
     *  Note that the saved state is supposed to be the *complete and exact*
     *  state of the simulator at the moment `save_state()` was called. For example,
     *  if the state was saved while the simulator was in initialisation mode
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
     *  The imported state is added to the simulator's internal list of saved
     *  states. Use `restore_state()` to restore it again. The state must have
     *  been saved by a simulator of the same or a compatible type.
     */
    virtual state_index import_state(const serialization::node& exportedState) = 0;
};

} // namespace cosim

#endif
