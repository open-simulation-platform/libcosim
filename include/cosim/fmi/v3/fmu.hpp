/**
 *  \file
 *  Classes for dealing with FMI 3.0 FMUs.
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_FMI_V3_FMU_HPP
#define COSIM_FMI_V3_FMU_HPP

#include <cosim/file_cache.hpp>
#include <cosim/fmi/fmu.hpp>
#include <cosim/fmi/importer.hpp>
#include <cosim/fs_portability.hpp>
#include <cosim/model_description.hpp>
#include <cosim/serialization.hpp>
#include <cosim/time.hpp>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>


struct fmi3_import_t;
using fmi3_FMU_state_t = void*;


namespace cosim::fmi
{
#ifdef _WIN32
namespace detail
{
class additional_path;
}
#endif

namespace v3
{

class fmu;
class slave_instance;


/**
 *  Information exchanged with a Co-Simulation intermediate-update callback.
 *
 *  The callback is invoked while the FMU is between communication points.
 *  The access flags describe which intermediate operations are currently
 *  permitted. Set `early_return_requested` only when `can_return_early` is
 *  true; the requested time must be between the current step start and end.
 */
struct intermediate_update_info
{
    /// Simulation time at which the callback was invoked.
    time_point time;

    /// The FMU requested that the callback may set intermediate variables.
    bool intermediate_variable_set_requested = false;

    /// The FMU permits the callback to read intermediate variables.
    bool intermediate_variable_get_allowed = false;

    /// The FMU has completed its intermediate-update work for this step.
    bool intermediate_step_finished = false;

    /// The FMU permits the callback to request an early return.
    bool can_return_early = false;

    /// Set by the callback to request an early return from the current step.
    bool early_return_requested = false;

    /// The requested early-return time.
    time_point early_return_time;
};


/**
 *  Options and callbacks used by an FMI 3.0 Co-Simulation instance.
 *
 *  The required intermediate variables default to all continuous input and
 *  output variables marked `intermediateUpdate` in the FMU description.
 */
struct co_simulation_callbacks
{
    /// Optional value references to expose during intermediate-update mode.
    std::vector<value_reference> required_intermediate_variables;

    /// Called for each intermediate-update callback from the FMU.
    std::function<void(slave_instance&, intermediate_update_info&)>
        intermediate_update;

    /**
     *  Whether the importer may use Event Mode with this instance
     *  (FMI `eventModeUsed`).
     *
     *  The default is to use Event Mode whenever the FMU supports it. Setting
     *  this to `false` instantiates an FMU that has `hasEventMode` as if it had
     *  none: the instance exposes no event controller, the FMU handles its own
     *  discrete events, and it can be mixed with simulators of different step
     *  sizes without event coordination. It is an error for an FMU with Clock
     *  variables, which require Event Mode.
     */
    bool event_mode_used = true;

    /// Enable early return when the FMU supports it. The caller must handle
    /// shortened steps through `do_step_with_info()` or an event-aware algorithm.
    bool early_return_allowed = false;
};


/// Describes how an FMI 3 Clock's activation interval is determined.
enum class clock_interval_variability // NOLINT(performance-enum-size): Preserve the public ABI.
{
    /// The FMU does not provide enough information to schedule this Clock.
    unknown,

    /// The interval never changes.
    constant,

    /// The interval is fixed for the current simulation.
    fixed,

    /// The host may tune the interval through the FMI interface.
    tunable,

    /// The FMU may report a new interval after activation.
    changing,

    /// The interval counts down after each activation.
    countdown,

    /// The Clock is activated explicitly by an event.
    triggered,
};


/// Clock scheduling metadata imported from an FMI 3 model description.
struct clock_description
{
    /// The Clock's model-variable name.
    std::string name;

    /// The FMI value reference of the Clock.
    value_reference reference;

    /// Whether the Clock is an input or output of the FMU.
    variable_causality causality;

    /// How the Clock's interval is determined.
    clock_interval_variability interval_variability;

    /// Whether the Clock can be deactivated by the host.
    bool can_be_deactivated;

    /// Optional FMI scheduling priority.
    std::optional<std::uint32_t> priority;

    /// Decimal interval, when the FMU declares one.
    std::optional<double> interval_decimal;

    /// Whether the Clock declares a fractional interval representation.
    bool supports_fraction = false;

    /// Fractional interval resolution, when available.
    std::optional<std::uint64_t> resolution;

    /// Decimal shift applied to the Clock's activation time, when available.
    std::optional<double> shift_decimal;

    /// Fractional interval counter, when available.
    std::optional<std::uint64_t> interval_counter;

    /// Fractional shift counter, when available.
    std::optional<std::uint64_t> shift_counter;
};


struct callback_context;


/**
 *  Represents an imported FMI 3.0 FMU.
 *
 *  The implementation supports the bounded FMI 3.0 Co-Simulation subset.
 *  FMUs that implement only Model Exchange or Scheduled Execution are
 *  rejected. An `fmu` owns the imported model description and remains alive
 *  while its instances use the extracted FMU files.
 */
class fmu : public fmi::fmu, public std::enable_shared_from_this<fmu>
{
private:
    friend class fmi::importer;
    friend class slave_instance;
    friend struct callback_context;
    fmu(
        std::shared_ptr<fmi::importer> importer,
        std::unique_ptr<file_cache::directory_ro> fmuDir);

public:
    fmu(const fmu&) = delete;
    fmu& operator=(const fmu&) = delete;
    fmu(fmu&&) = delete;
    fmu& operator=(fmu&&) = delete;

    ~fmu() override;

    fmi::fmi_version fmi_version() const override;

    std::shared_ptr<const cosim::model_description>
    model_description() const override;

    std::shared_ptr<fmi::slave_instance> instantiate_slave(
        std::string_view instanceName) override
    {
        co_simulation_callbacks options;
        options.early_return_allowed = true;
        return std::static_pointer_cast<fmi::slave_instance>(
            instantiate_v3_slave(instanceName, std::move(options)));
    }

    std::shared_ptr<fmi::importer> importer() const override;

    /**
     *  Creates a new FMI 3.0 Co-Simulation instance.
     *
     *  The returned instance starts in the instantiated state. Optional
     *  callbacks are used for Intermediate Update access and are kept alive
     *  for the lifetime of the instance.
     */
    std::shared_ptr<v3::slave_instance> instantiate_v3_slave(
        std::string_view instanceName,
        co_simulation_callbacks callbacks = {});

    /// Returns the Clock metadata parsed from the FMU description.
    const std::vector<clock_description>& clock_descriptions() const;

    /// Returns the Co-Simulation capability `mightReturnEarlyFromDoStep`.
    bool might_return_early_from_do_step() const noexcept;

    /// Returns the Co-Simulation capability `canReturnEarlyAfterIntermediateUpdate`.
    bool can_return_early_after_intermediate_update() const noexcept;

    /// Returns the directory containing the unpacked FMU resources.
    cosim::filesystem::path directory() const;

    /// Returns the underlying FMI Library handle for internal integrations.
    fmi3_import_t* fmilib_handle() const;

private:
    const variable_description* find_variable(value_reference reference) const;
    const variable_description* find_variable(variable_type type, value_reference reference) const;
    std::shared_ptr<fmi::importer> importer_;
    std::unique_ptr<file_cache::directory_ro> dir_;

    fmi3_import_t* handle_;
    cosim::model_description modelDescription_;
    std::unordered_map<value_reference, const variable_description*> variablesByReference_;
    std::unordered_map<std::string, std::unordered_set<std::int64_t>> enumItemsByType_;
    bool mightReturnEarlyFromDoStep_ = false;
    bool canReturnEarlyAfterIntermediateUpdate_ = false;
    std::vector<clock_description> clockDescriptions_;
    std::vector<std::weak_ptr<slave_instance>> instances_;
    std::atomic_bool fatal_{false};

#ifdef _WIN32
    std::unique_ptr<detail::additional_path> additionalDllSearchPath_;
#endif
};


/**
 *  An FMI 3.0 Co-Simulation instance exposed through the `cosim::slave`
 *  contract.
 *
 *  The instance follows the FMI 3.0 lifecycle: setup, initialization,
 *  simulation, event handling, and termination. Typed access functions require
 *  matching value-reference and value spans; whole-value functions additionally
 *  validate the resolved array shape.
 */
class slave_instance : public fmi::slave_instance, public cosim::clock_controller, public std::enable_shared_from_this<slave_instance> // NOLINT(misc-multiple-inheritance): The public slave and Clock interfaces are distinct.
{
private:
    friend std::shared_ptr<slave_instance> fmu::instantiate_v3_slave(
        std::string_view /*instanceName*/,
        co_simulation_callbacks /*callbacks*/);
    friend void intermediate_update(
        void* /*environment*/,
        double /*intermediateUpdateTime*/,
        bool /*intermediateVariableSetRequested*/,
        bool /*intermediateVariableGetAllowed*/,
        bool /*intermediateStepFinished*/,
        bool /*canReturnEarly*/,
        bool* /*earlyReturnRequested*/,
        double* /*earlyReturnTime*/) noexcept;

    enum class state // NOLINT(performance-enum-size): Preserve the class layout.
    {
        instantiated,
        initialization,
        step,
        event,
        terminated,
        configuration,
    };

    slave_instance(
        std::shared_ptr<v3::fmu> fmu,
        std::string_view instanceName,
        co_simulation_callbacks callbacks);

public:
    slave_instance(const slave_instance&) = delete;
    slave_instance& operator=(const slave_instance&) = delete;
    slave_instance(slave_instance&&) = delete;
    slave_instance& operator=(slave_instance&&) = delete;

    ~slave_instance() noexcept override;

    /**
     *  Enables or disables FMU logging for the given category names.
     *
     *  An empty span applies to all categories. Nonempty names must match
     *  the FMU's `LogCategories` declarations; undeclared names are rejected
     *  before calling the FMU. Do not call from an FMI callback.
     */
    void set_debug_logging(bool enabled, gsl::span<const std::string_view> categories = {});

    /// Returns the Clock-routing interface implemented by this instance.
    cosim::clock_controller* get_clock_controller() noexcept override;

    std::optional<cosim::clock_description> resolved_input_clock_schedule(
        value_reference reference) const override;

    clock_interval_update get_input_clock_interval(value_reference reference) override;
    bool has_pending_clock_intervals() const override;
    std::optional<clock_interval_update> pending_input_clock_interval(
        value_reference reference) const override;
    void apply_pending_clock_intervals() override;

    /**
     * Configure a fixed input Clock before simulation starts. The interval and
     * shift are applied through FMI in Initialization Mode.
     */
    void configure_fixed_clock_decimal(
        value_reference reference, double interval, double shift = 0.0);

    /// Configure an exact fractional interval and shift with a shared resolution.
    void configure_fixed_clock_fraction(
        value_reference reference, std::uint64_t intervalCounter,
        std::uint64_t shiftCounter, std::uint64_t resolution);

    /// Return a fixed input Clock to its declared or FMU-provided schedule.
    void clear_fixed_clock_configuration(value_reference reference);

    /// Configure a host-controlled tunable Clock before initialization completes.
    void configure_tunable_clock_decimal(
        value_reference reference, double interval, double shift = 0.0);

    /// Configure a tunable Clock using fractional interval and shift counters.
    void configure_tunable_clock_fraction(
        value_reference reference, std::uint64_t intervalCounter,
        std::uint64_t shiftCounter, std::uint64_t resolution);

    /// Remove the configured tunable schedule before initialization completes.
    void clear_tunable_clock_configuration(value_reference reference);

    /// Queue a tunable interval change for the next coordinated Event Mode.
    void set_tunable_clock_interval_decimal(value_reference reference, double interval);

    /// Queue a fractional interval change without changing the Clock's representation.
    void set_tunable_clock_interval_fraction(
        value_reference reference, std::uint64_t counter, std::uint64_t resolution);

    /// Returns the Event Mode interface when Event Mode is enabled.
    cosim::event_controller* get_event_controller() noexcept override;

    /// Returns whether the FMU is currently in Event Mode.
    bool is_in_event_mode() const noexcept override;

    /// Sets the positive limit for internally managed event iteration.
    void set_max_event_iterations(std::size_t limit) override;

    /// Intermediate-update callbacks are not automatically replayed.
    bool supports_automatic_step_replay() const noexcept override;

    /**
     *  Enters FMI initialization mode.
     *
     *  This must be called once before `start_simulation()`. The start and
     *  stop times and tolerance are forwarded to the FMU.
     */
    void setup(
        time_point startTime,
        std::optional<time_point> stopTime,
        std::optional<double> relativeTolerance) override;

    /// Leaves initialization mode; clocked instances remain in initial Event Mode.
    void start_simulation() override;

    /// Terminates the FMU simulation; instance resources are released on destruction.
    void end_simulation() override;

    /**
     *  Performs one FMI 3.0 Co-Simulation step.
     *
     *  Instances using Event Mode or early return must use
     *  `do_step_with_info()` so their control flags are not discarded.
     */
    step_result do_step(time_point currentT, duration deltaT) override;

    /// Performs a step and returns its detailed FMI control outcome.
    step_result_info do_step_with_info(
        time_point currentT,
        duration deltaT) override;

    /// Completes Event Mode processing requested by the FMU.
    void handle_events() override;

    event_handling_status get_event_handling_status() const noexcept override;

    /**
     *  Enters Configuration Mode before initialization or Reconfiguration Mode
     *  from Step Mode. Runtime entry requires a tunable structural parameter,
     *  and only tunable structural parameters may be set at runtime.
     */
    void enter_configuration_mode();

    /// Returns to Instantiated or Step Mode, respectively.
    void exit_configuration_mode();

    /// Enters Event Mode after a step or Clock event requests it.
    void enter_event_mode() override;

    /**
     *  Flags returned by FMI `UpdateDiscreteStates`.
     *
     *  `next_event_time` is present only when the FMU reports a scheduled
     *  event. The remaining flags indicate which continuous or discrete values
     *  must be refreshed before the next mode transition.
     */
    struct discrete_state_update
    {
        bool discrete_states_need_update = false;
        bool terminate_simulation = false;
        bool nominals_of_continuous_states_changed = false;
        bool values_of_continuous_states_changed = false;
        std::optional<time_point> next_event_time;
    };

    /// Runs FMI `UpdateDiscreteStates` and returns its control flags.
    discrete_state_update update_discrete_states();

    /// Enters Step Mode after Event Mode processing is complete.
    void enter_step_mode() override;

    /// Resets the FMU and returns the instance to its initial lifecycle state.
    void reset();

    /// Reports whether this FMU uses Event Mode during Co-Simulation.
    bool event_mode_used() const noexcept;

    /// Reports whether the FMU permits an early return from `DoStep`.
    bool early_return_allowed() const noexcept;

    /// Reports whether Intermediate Update callbacks are available.
    bool provides_intermediate_update() const noexcept;

    /// Returns the next event time reported by the FMU, if one is available.
    std::optional<time_point> next_event_time() const noexcept;

    /// Returns the Clock metadata associated with this instance's FMU.
    const std::vector<clock_description>& clock_descriptions() const;

    /**
     *  Reads Clock activation values.
     *
     *  The output span must have one element for each value reference. Clock
     *  activation is state-dependent; invalid references or lifecycle states
     *  are reported as errors.
     */
    void get_clocks(
        gsl::span<const value_reference> clocks,
        gsl::span<bool> values) const;
    /**
     *  Sets input Clock activation values.
     *
     *  This operation is valid only in an FMI mode that permits Clock input
     *  access.
     */
    void set_clocks(
        gsl::span<const value_reference> clocks,
        gsl::span<const bool> values);
    /// Reads one output Clock for the Clock-routing interface.
    bool get_output_clock(value_reference reference) override;

    /// Activates or deactivates one input Clock for the current event instant.
    void set_input_clock(value_reference reference, bool active) override;

    /// Reads a value associated with an active Clock.
    variable_value get_clocked_variable(
        variable_type type,
        value_reference reference) override;
    /// Sets a value associated with an active Clock.
    void set_clocked_variable(
        variable_type type,
        value_reference reference,
        const variable_value& value) override;
    /// Updates discrete Clock state and returns the next event information.
    clock_update_result update_clock_state() override;
    /**
     *  Resolves an FMI variable's current array shape.
     *
     *  Structural parameters are read from the FMU when required. An empty
     *  result represents a scalar; unsupported or invalid shapes are errors.
     */
    std::vector<std::uint64_t> resolved_shape(
        variable_type type,
        value_reference variable) const;

    /**
     *  Reads typed scalar variables.
     *
     *  Each value-reference span must be paired with an equally sized output
     *  span. String results are copied out of FMU-owned storage before they are
     *  returned to the caller.
     */
    void get_real_variables(
        gsl::span<const value_reference> variables,
        gsl::span<double> values) const override;
    /// Reads Float32 scalars; see get_real_variables() for span requirements.
    void get_float32_variables(
        gsl::span<const value_reference> variables,
        gsl::span<float> values) const;
    /// Reads Int8 scalars; see get_real_variables() for span requirements.
    void get_int8_variables(
        gsl::span<const value_reference> variables,
        gsl::span<std::int8_t> values) const;
    /// Reads UInt8 scalars; see get_real_variables() for span requirements.
    void get_uint8_variables(
        gsl::span<const value_reference> variables,
        gsl::span<std::uint8_t> values) const;
    /// Reads Int16 scalars; see get_real_variables() for span requirements.
    void get_int16_variables(
        gsl::span<const value_reference> variables,
        gsl::span<std::int16_t> values) const;
    /// Reads UInt16 scalars; see get_real_variables() for span requirements.
    void get_uint16_variables(
        gsl::span<const value_reference> variables,
        gsl::span<std::uint16_t> values) const;
    void get_integer_variables(
        gsl::span<const value_reference> variables,
        gsl::span<int> values) const override;
    /// Reads UInt32 scalars; see get_real_variables() for span requirements.
    void get_uint32_variables(
        gsl::span<const value_reference> variables,
        gsl::span<std::uint32_t> values) const;
    /// Reads Int64 scalars; see get_real_variables() for span requirements.
    void get_int64_variables(
        gsl::span<const value_reference> variables,
        gsl::span<std::int64_t> values) const;
    /// Reads UInt64 scalars; see get_real_variables() for span requirements.
    void get_uint64_variables(
        gsl::span<const value_reference> variables,
        gsl::span<std::uint64_t> values) const;
    void get_boolean_variables(
        gsl::span<const value_reference> variables,
        gsl::span<bool> values) const override;
    void get_string_variables(
        gsl::span<const value_reference> variables,
        gsl::span<std::string> values) const override;
    /// Reads Binary scalars into caller-owned values.
    void get_binary_variables(
        gsl::span<const value_reference> variables,
        gsl::span<binary_value> values) const;
    /// Reads Enumeration scalars; see get_real_variables() for span requirements.
    void get_enumeration_variables(
        gsl::span<const value_reference> variables,
        gsl::span<enumeration_value> values) const;

    /**
     *  Sets typed scalar variables.
     *
     *  Each value-reference span must be paired with an equally sized value
     *  span. The exact FMI type and access permissions are checked before the
     *  FMU is called.
     */
    void set_real_variables(
        gsl::span<const value_reference> variables,
        gsl::span<const double> values) override;
    /// Writes Float32 scalars; see set_real_variables() for span requirements.
    void set_float32_variables(
        gsl::span<const value_reference> variables,
        gsl::span<const float> values);
    /// Writes Int8 scalars; see set_real_variables() for span requirements.
    void set_int8_variables(
        gsl::span<const value_reference> variables,
        gsl::span<const std::int8_t> values);
    /// Writes UInt8 scalars; see set_real_variables() for span requirements.
    void set_uint8_variables(
        gsl::span<const value_reference> variables,
        gsl::span<const std::uint8_t> values);
    /// Writes Int16 scalars; see set_real_variables() for span requirements.
    void set_int16_variables(
        gsl::span<const value_reference> variables,
        gsl::span<const std::int16_t> values);
    /// Writes UInt16 scalars; see set_real_variables() for span requirements.
    void set_uint16_variables(
        gsl::span<const value_reference> variables,
        gsl::span<const std::uint16_t> values);
    void set_integer_variables(
        gsl::span<const value_reference> variables,
        gsl::span<const int> values) override;
    /// Writes UInt32 scalars; see set_real_variables() for span requirements.
    void set_uint32_variables(
        gsl::span<const value_reference> variables,
        gsl::span<const std::uint32_t> values);
    /// Writes Int64 scalars; see set_real_variables() for span requirements.
    void set_int64_variables(
        gsl::span<const value_reference> variables,
        gsl::span<const std::int64_t> values);
    /// Writes UInt64 scalars; see set_real_variables() for span requirements.
    void set_uint64_variables(
        gsl::span<const value_reference> variables,
        gsl::span<const std::uint64_t> values);
    void set_boolean_variables(
        gsl::span<const value_reference> variables,
        gsl::span<const bool> values) override;
    void set_string_variables(
        gsl::span<const value_reference> variables,
        gsl::span<const std::string> values) override;
    /// Writes Binary scalars; see set_real_variables() for span requirements.
    void set_binary_variables(
        gsl::span<const value_reference> variables,
        gsl::span<const binary_value> values);
    /// Writes Enumeration scalars; see set_real_variables() for span requirements.
    void set_enumeration_variables(
        gsl::span<const value_reference> variables,
        gsl::span<const enumeration_value> values);
    /**
     *  Reads complete scalar or array values for typed references.
     *
     *  The returned values preserve request order and own all data, including
     *  strings and binary payloads.
     */
    std::vector<variable_value> get_variable_values(
        gsl::span<const typed_value_reference> variables) const override;
    /**
     *  Sets complete scalar or array values for typed references.
     *
     *  The complete batch is checked before dispatch. Values must match the
     *  declared exact type and resolved shape, accounting for preceding
     *  structural-parameter assignments in batch order. An FMU failure can
     *  still leave earlier successful assignments applied.
     */
    /// Validates the batch before writing; an FMU failure can leave earlier writes applied.
    void set_variable_values(
        gsl::span<const typed_value_reference> variables,
        gsl::span<const variable_value> values) override;

    /// Saves the complete FMI instance state and returns a local handle.
    state_index save_state() override;

    /// Replaces an existing saved state with the current FMI instance state.
    void save_state(state_index stateIndex) override;

    /// Restores a previously saved FMI instance state.
    void restore_state(state_index stateIndex) override;

    /// Releases resources associated with a saved state.
    void release_state(state_index stateIndex) override;

    /// Serializes a saved state when the FMU advertises export support.
    serialization::node export_state(state_index stateIndex) const override;

    /// Imports a serialized state and returns a local saved-state handle.
    state_index import_state(const serialization::node& exportedState) override;

    std::shared_ptr<fmi::fmu> fmu() const override
    {
        return std::static_pointer_cast<fmi::fmu>(v3_fmu());
    }

    std::shared_ptr<v3::fmu> v3_fmu() const;
    fmi3_import_t* fmilib_handle() const;

private:
    struct saved_state // NOLINT(bugprone-exception-escape): MSVC unordered-container moves may allocate.
    {
        fmi3_FMU_state_t fmuState = nullptr;
        bool setupComplete = false;
        bool simStarted = false;
        state lifecycleState = state::instantiated;
        state stateBeforeConfiguration = state::instantiated;
        time_point currentTime;
        std::optional<time_point> stopTime;
        bool terminationRequested = false;
        bool eventHandlingRequested = false;
        bool stepSetStarted = false;
        std::optional<time_point> nextEventTime;
        bool eventIterationComplete = false;
        std::unordered_set<value_reference> activeClocks;
        std::unordered_set<value_reference> activatedPeriodicClocks;
        std::unordered_set<value_reference> changingClocksAwaitingInterval;
        std::unordered_map<value_reference, cosim::clock_description>
            resolvedInputClockSchedules;
        std::unordered_map<value_reference, cosim::clock_description>
            configuredFixedClockSchedules;
        std::unordered_map<value_reference, clock_interval_update> initialClockIntervals;
        std::unordered_map<value_reference, clock_interval_update> pendingClockIntervals;
        bool hasDimensionValues = false;
        std::unordered_map<value_reference, std::uint64_t> dimensionValues;
    };

    void copy_current_state(saved_state& state);
    void prepare_intermediate_callbacks();
    void validate_clock_support(bool eventModeSupported) const;
    void parse_state_lifecycle(const serialization::node& exportedState, int schemeVersion, saved_state& state) const;
    void parse_state_timing_and_event_flags(const serialization::node& exportedState, int schemeVersion, saved_state& state) const;
    void parse_state_next_event_time(const serialization::node& exportedState, int schemeVersion, saved_state& state) const;
    void parse_state_clocks(const serialization::node& exportedState, int schemeVersion, saved_state& state) const;
    void parse_state_clock_schedules(const serialization::node& exportedState, int schemeVersion, saved_state& state) const;
    void parse_state_clock_schedule_entries(const serialization::node& entries,
        std::unordered_map<value_reference, cosim::clock_description>& schedules, bool configured) const;
    void parse_state_clock_schedule_entry(const serialization::node::value_type& entry,
        std::unordered_map<value_reference, cosim::clock_description>& schedules, bool configured) const;
    void parse_state_initial_clock_intervals(const serialization::node& exportedState, saved_state& state) const;
    void validate_state_clock_schedules(const saved_state& state) const;
    void parse_state_pending_clock_intervals(const serialization::node& exportedState, saved_state& state) const;
    void parse_state_changing_clocks(const serialization::node& exportedState, saved_state& state) const;
    void parse_state_dimensions(const serialization::node& exportedState, saved_state& state) const;
    state_index store_new_state(saved_state state);
    void notify_intermediate_update(
        double intermediateUpdateTime,
        bool intermediateVariableSetRequested,
        bool intermediateVariableGetAllowed,
        bool intermediateStepFinished,
        bool canReturnEarly,
        bool* earlyReturnRequested,
        double* earlyReturnTime);
    void check_intermediate_access(
        gsl::span<const value_reference> variables,
        bool setting) const;
    std::size_t check_variable_access(
        gsl::span<const value_reference> variables,
        variable_type type,
        bool setting) const;
    std::size_t check_variable_access(
        gsl::span<const value_reference> variables,
        variable_type type,
        bool setting,
        const std::unordered_map<value_reference, std::uint64_t>& structuralParameterValues) const;
    void validate_enumeration_values(
        gsl::span<const value_reference> variables,
        gsl::span<const enumeration_value> values,
        const std::unordered_map<value_reference, std::uint64_t>& structuralParameterValues,
        const char* operation) const;
    void check_clocked_variable_access(
        const variable_description& variable,
        const char* operation) const;
    void deactivate_clock_and_dependents(value_reference clock);
    void refresh_structural_parameters();
    void resolve_fixed_clock_schedules();
    void resolve_fixed_clock_schedule(const clock_description& clock);
    void resolve_fractional_clock_schedule(const clock_description& clock, cosim::clock_description& schedule);
    void resolve_decimal_clock_schedule(const clock_description& clock, cosim::clock_description& schedule);
    void apply_fixed_clock_schedule(const clock_description& clock);
    void resolve_changing_clock_schedules();
    cosim::clock_description fixed_clock_for_configuration(
        value_reference reference, const char* operation) const;
    cosim::clock_description tunable_clock_for_configuration(
        value_reference reference, const char* operation) const;
    cosim::clock_description tunable_clock_for_update(
        value_reference reference, const char* operation) const;
    void validate_step_endpoint(
        time_point currentT,
        duration deltaT,
        double lastSuccessfulTime,
        bool communicationStopped) const;

    std::shared_ptr<v3::fmu> fmu_;
    fmi3_import_t* handle_;
    bool setupComplete_ = false;
    bool simStarted_ = false;
    state state_ = state::instantiated;
    state stateBeforeConfiguration_ = state::instantiated;
    time_point currentTime_;
    std::optional<time_point> stopTime_;
    bool eventModeUsed_ = false;
    bool earlyReturnAllowed_ = false;
    bool warnedEndpointEarlyReturn_ = false;
    bool providesIntermediateUpdate_ = false;
    bool terminationRequested_ = false;
    bool eventHandlingRequested_ = false;
    bool terminationAttempted_ = false;
    bool stepSetStarted_ = false;
    std::optional<time_point> nextEventTime_;
    bool eventIterationComplete_ = false;
    std::size_t maxEventIterations_ = default_max_event_iterations;
    mutable std::unordered_set<value_reference> activeClocks_;
    std::unordered_set<value_reference> activatedPeriodicClocks_;
    std::unordered_set<value_reference> changingClocksAwaitingInterval_;
    std::unordered_map<value_reference, cosim::clock_description>
        resolvedInputClockSchedules_;
    std::unordered_map<value_reference, cosim::clock_description>
        configuredFixedClockSchedules_;
    std::unordered_map<value_reference, clock_interval_update> pendingClockIntervals_;
    std::unordered_map<value_reference, clock_interval_update> initialClockIntervals_;
    std::unordered_map<value_reference, std::uint64_t>
        structuralParameterValues_;
    co_simulation_callbacks callbacks_;
    std::unordered_set<value_reference> intermediateReferences_;

    struct intermediate_state
    {
        double startTime = 0.0;
        double endTime = 0.0;
        double lastCompletedTime = 0.0;
        bool variableSetRequested = false;
        bool variableGetAllowed = false;
        bool setStarted = false;
    };

    mutable std::optional<intermediate_state> intermediateState_;
    mutable std::recursive_mutex mutex_;
    std::string instanceName_;
    std::unique_ptr<callback_context> callbackContext_;
    std::vector<saved_state> savedStates_;
    std::queue<state_index> savedStatesFreelist_;
};


} // namespace v3
} // namespace cosim::fmi
#endif // COSIM_FMI_V3_FMU_HPP
