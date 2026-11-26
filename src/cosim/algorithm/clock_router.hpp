/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef LIBCOSIM_ALGORITHM_CLOCK_ROUTER_HPP
#define LIBCOSIM_ALGORITHM_CLOCK_ROUTER_HPP

#include "event_iteration_budget.hpp"

#include "cosim/algorithm/simulator.hpp"
#include "cosim/event_iteration.hpp"
#include "cosim/execution.hpp"
#include "cosim/model_description.hpp"
#include "cosim/serialization.hpp"
#include "cosim/slave.hpp"
#include "cosim/time.hpp"

#include <boost/multiprecision/cpp_int.hpp> // IWYU pragma: keep

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cosim
{

/**
 * Routes the bounded FMI 3 Clock subset used by time-stepped algorithms.
 *
 * Clock edges and Clock-associated data are deliberately kept separate from
 * ordinary communication-point transfers. The router is only active when a
 * simulator advertises the optional Clock capability.
 * Registered simulators and the shared event budget are non-owning. Scheduled
 * ticks and staged data are coordinated at each physical event time.
 */
class clock_router
{
    friend struct clock_router_test_access;

public:
    /// Uses the shared per-time event budget; it must outlive the router.
    void set_event_budget(event_iteration_budget& budget) noexcept
    {
        eventBudget_ = &budget;
    }

    void set_event_value_tolerance(event_value_tolerance tolerance) noexcept
    {
        eventValueTolerance_ = tolerance;
    }

    /**
     * Installs a callback that runs just before an input Clock is activated.
     * It lets the caller bring paths outside the router, such as functions,
     * up to date so that the Clock-associated inputs sample the current
     * values when the Clock activates. Pass an empty function to remove it.
     */
    void set_before_input_activation(std::function<void()> hook);

    /// Throws if a failed Clock event requires snapshot recovery or recreation.
    void check_recovery() const;

    struct clock_connection
    {
        variable_id source;
        variable_id target;
    };

    struct data_connection
    {
        variable_id source;
        variable_id target;
    };

    /// Registers a non-owning simulator and its optional Clock controller.
    void add_simulator(simulator_index index, simulator* sim,
        std::shared_ptr<const model_description> description = {});

    void remove_simulator(simulator_index index);

    /**
     * Registers a simulator-to-simulator value connection.
     *
     * Returns true when the connection is associated with at least one Clock
     * and therefore must not use ordinary communication-point transfer.
     */
    bool connect_data(variable_id source, variable_id target);

    /// Connects triggered output and input Clocks, validating their endpoints.
    void connect_clock(variable_id source, variable_id target);

    /**
     * Removes a Clock or Clock-associated data connection to an input.
     */
    bool disconnect(variable_id input);

    /// Visits the simulator endpoints of Clock and Clock-associated connections.
    template<typename Visitor>
    // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward): The visitor is reused as an lvalue.
    void for_each_connection(Visitor&& visit) const
    {
        for (const auto& connection : clock_connections_) {
            visit(connection.source.simulator, connection.target.simulator);
        }
        for (const auto& connection : clocked_data_connections_) {
            visit(connection.source.simulator, connection.target.simulator);
        }
    }

    /// Reports whether any registered simulator or connection involves Clocks.
    [[nodiscard]] bool has_clock_support() const;

    /// Reports whether Clock coordination requested simulation termination.
    [[nodiscard]] bool termination_requested() const noexcept
    {
        return terminated_;
    }

    /**
     * Builds the initial periodic schedule and completes initial Clock mode.
     */
    void initialize(time_point startTime);

    /**
     * Caps a proposed algorithm step at the next known Clock instant.
     */
    [[nodiscard]] duration cap_step(time_point currentTime, duration proposed) const;

    // Preflight before participants advance; event processing repeats the
    // check in case an earlier event changed a pending interval.
    void validate_pending_intervals(time_point currentTime) const;

    // Keep the latest value from each current ordinary source in the target's
    // Clock input cache before any source can advance beyond this instant.
    void stage_current_unclocked_data(
        const std::unordered_set<simulator_index>& participants);

    /**
     * Processes an event that is already due at the current communication time.
     * When `participants` is given, only those simulators handle events.
     */
    bool prepare_step(
        time_point currentTime,
        const std::unordered_set<simulator_index>* participants = nullptr);

    /**
     * Processes a post-step event when FMI requested event handling or a
     * declared periodic/model event has reached the communication point.
     * When `participants` is given, only those simulators handle events.
     */
    bool complete_step(
        time_point currentTime,
        const std::unordered_set<simulator_index>& eventSources,
        const std::unordered_set<simulator_index>* participants = nullptr);

    // Clock events are routed for all Clock-capable simulators together, so
    // none of them may be ahead of the communication point.
    void require_clock_participants(
        const std::unordered_set<simulator_index>* participants) const;

    void check_input_event_supported(time_point /*unused*/) const;

    bool process_input_event(
        time_point currentTime,
        const std::unordered_set<simulator_index>* participants = nullptr);

    [[nodiscard]] serialization::node export_state() const;

    void import_state(const serialization::node& state);

private:
    static constexpr unsigned int max_clock_denominator_msb = 4096;

    [[nodiscard]] bool clock_feedback_path_exists(const data_connection& data) const;

    [[nodiscard]] bool due_clock_model_event(time_point currentTime) const;

    bool process_legacy_events(
        time_point currentTime,
        const std::unordered_set<simulator_index>& eventSources,
        const std::unordered_set<simulator_index>* participants = nullptr);

    struct simulator_info
    {
        simulator* sim = nullptr;
        std::shared_ptr<const model_description> description;
        clock_controller* controller = nullptr;
        bool has_clock_variables = false;
    };

    struct fractional_schedule
    {
        time_point origin;
        std::uint64_t interval;
        std::uint64_t resolution;
        std::uint64_t shift;
        std::uint64_t tick;
        std::optional<double> decimal_shift;
    };

    struct decimal_schedule
    {
        time_point origin;
        double interval;
        double shift;
        std::uint64_t tick;
        bool legacy;
    };

    struct periodic_clock
    {
        variable_id id{};
        duration interval{};
        time_point next;
        std::optional<fractional_schedule> fraction;
        std::optional<decimal_schedule> decimal;
    };

    struct changing_clock
    {
        variable_id id;
        clock_interval_update interval;
        time_point origin;
        std::optional<time_point> next;
        bool countdown;
        bool known;
        boost::multiprecision::cpp_int origin_numerator;
        boost::multiprecision::cpp_int origin_denominator;
        bool tunable;
        bool fmu_controlled;
        bool ticked;
    };

    [[nodiscard]] std::vector<periodic_clock> parse_periodic_clock_state(
        const serialization::node& state, std::uint32_t version) const;
    [[nodiscard]] std::vector<changing_clock> parse_changing_clock_state(
        const serialization::node& state, std::uint32_t version) const;
    static void restore_fractional_clock(
        const serialization::node& entry, time_point next, fractional_schedule& schedule);
    static void restore_decimal_clock(
        const serialization::node& entry, std::uint32_t version,
        time_point next, duration interval, decimal_schedule& schedule);
    void restore_changing_clock(
        const serialization::node& entry, std::uint32_t version, changing_clock& clock) const;
    void commit_clock_state(const serialization::node& state,
        std::vector<periodic_clock> clocks, std::vector<changing_clock> changing);

    void validate_clock_feedback() const;

    void initialize_changing_clock(simulator_index index, const simulator_info& info,
        const variable_description& variable, const clock_description& description, time_point startTime);

    void initialize_periodic_clock(simulator_index index, const simulator_info& info,
        const variable_description& variable, time_point startTime);

    void refresh_changing_clock_intervals(simulator_index index, clock_controller& controller,
        time_point currentTime, std::unordered_set<variable_id>& pendingReads);

    static void refresh_tunable_clock(changing_clock& clock, clock_controller& controller,
        time_point currentTime, bool activated);

    static void refresh_aperiodic_clock(changing_clock& clock, clock_controller& controller,
        time_point currentTime, bool activated);

    static void initialize_fractional_schedule(
        periodic_clock& scheduled, const clock_description& schedule, time_point startTime);

    static void initialize_decimal_schedule(
        periodic_clock& scheduled, const clock_description& schedule,
        double interval, time_point startTime);

    static std::pair<boost::multiprecision::cpp_int, boost::multiprecision::cpp_int>
    binary_fraction(double value);

    static boost::multiprecision::cpp_int parse_clock_integer(
        const std::string& value, bool signedValue);

    static time_point rounded_clock_time(
        const boost::multiprecision::cpp_int& numerator,
        const boost::multiprecision::cpp_int& denominator,
        bool fractional);

    static std::pair<boost::multiprecision::cpp_int, boost::multiprecision::cpp_int>
    changing_exact_time(const changing_clock& clock);

    static time_point changing_time(const changing_clock& clock);

    static void validate_tunable_interval(const clock_interval_update& interval);

    static void activate_changing_clock(changing_clock& clock, time_point currentTime);

    static time_point decimal_time(const decimal_schedule& schedule);

    static std::optional<time_point> legacy_decimal_time(time_point origin, double offset);

    static duration legacy_decimal_interval(const decimal_schedule& schedule);

    static std::optional<time_point> legacy_decimal_start(const decimal_schedule& schedule);

    static time_point legacy_decimal_tick(const decimal_schedule& schedule, duration interval);

    static time_point fractional_time(const fractional_schedule& schedule);

    struct observed_clocked_data
    {
        variable_id source{};
        variable_value rawValue;
    };

    struct clock_event_work
    {
        std::unordered_set<variable_id> firedOutputs;
        std::unordered_set<variable_id> activatedInputs;
        std::unordered_set<variable_id> transferredData;
        std::unordered_set<std::size_t> propagatedConnections;
        std::unordered_set<variable_id> pendingChangingReads;
        std::vector<observed_clocked_data> observedData;
        std::optional<time_point> modelNextEvent;
        std::unordered_map<simulator_index, std::size_t> localIterations;
    };

    struct clock_event_update
    {
        bool updateRequested;
        bool terminationRequested;
    };

    bool enter_clock_event(time_point currentTime, bool roundAlreadyAcquired);
    [[nodiscard]] std::unordered_set<variable_id> due_clock_ids(time_point currentTime) const;
    void activate_due_clock_waves(time_point currentTime, clock_event_work& work);
    void activate_scheduled_changing_clock(changing_clock& clock, time_point currentTime, clock_event_work& work);
    void route_clock_event_outputs(clock_event_work& work);
    clock_event_update update_clock_event_participants(
        time_point currentTime, clock_event_work& work, std::size_t maxLocalIterations);
    bool activate_pending_changing_clocks(time_point currentTime, bool routingEnabled, clock_event_work& work);
    void finish_clock_event(time_point currentTime, const std::optional<time_point>& modelNextEvent);

    [[nodiscard]] const variable_description& describe(variable_id identifier) const;

    clock_controller& require_controller(simulator_index index) const; // NOLINT(modernize-use-nodiscard)

    void activate_input_clock(variable_id clock);

    [[nodiscard]] bool clock_routed(const data_connection& data) const;

    [[nodiscard]] bool depends_on_due_clock(
        variable_id clock,
        const std::unordered_set<variable_id>& pendingClocks) const;

    // Queries the output Clocks associated with a source variable that has
    // no routed Clock connection, and reports whether any associated Clock
    // is active.
    bool probe_source_clocks(
        variable_id source,
        std::unordered_set<variable_id>& firedOutputs,
        const std::unordered_set<variable_id>& activatedInputs);

    void deliver_data(variable_id target, const variable_value& value);

    // Delivers Clock-associated outputs whose targets are not activated by a
    // routed Clock connection, i.e. ordinary inputs and independently
    // scheduled Clock-associated inputs.
    void deliver_unrouted_data(
        std::unordered_set<variable_id>& firedOutputs,
        const std::unordered_set<variable_id>& activatedInputs,
        std::unordered_set<variable_id>& transferredData);

    // An FMU may deactivate an output Clock after another of its Clocks is
    // activated in the same superdense instant. The downstream triggered
    // Clocks that were activated through the retracted output are deactivated
    // too, which requires the FMU to declare canBeDeactivated.
    void retract_deactivated_outputs(
        simulator_index origin,
        std::unordered_set<variable_id>& firedOutputs,
        std::unordered_set<variable_id>& activatedInputs,
        std::unordered_set<variable_id>& transferredData,
        std::unordered_set<std::size_t>& propagatedConnections,
        std::vector<observed_clocked_data>& observedData);

    void clear_transfers_for_clock(variable_id target, std::unordered_set<variable_id>& transferredData) const;

    // Only a single path whose outputs are gated by their sole input Clock
    // has the same activation order regardless of connection registration.
    // Other topologies retain the original iterative traversal.
    const std::vector<std::size_t>& linear_clock_order();

    [[nodiscard]] std::vector<std::size_t> build_linear_clock_order() const;

    void verify_clocked_data_consistency(
        simulator_index target,
        const std::unordered_set<variable_id>& firedOutputs,
        const std::unordered_set<variable_id>& activatedInputs,
        const std::vector<observed_clocked_data>& observedData) const;

    template<typename Visit>
    bool propagate_outputs(
        // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): The existing friend-test interface passes distinct event sets.
        std::unordered_set<variable_id>& firedOutputs,
        std::unordered_set<variable_id>& activatedInputs,
        std::unordered_set<variable_id>& transferredData,
        std::unordered_set<std::size_t>& propagatedConnections,
        std::vector<observed_clocked_data>& observedData,
        // NOLINTNEXTLINE(cppcoreguidelines-missing-std-forward): The visitor is reused as an lvalue.
        Visit&& visit)
    {
        // Propagation is monotonic within one event iteration: each output
        // fires once and each input/data target is updated once.
        bool changed = false;
        const auto& order = linear_clock_order();
        const bool useOrder = !order.empty() && clocked_data_connections_.empty() &&
            !beforeInputActivation_;
        for (std::size_t position = 0; position < clock_connections_.size(); ++position) {
            const auto index = useOrder ? order.at(position) : position;
            visit(index);
            const auto& connection = clock_connections_.at(index);
            if (propagatedConnections.count(index) != 0) {
                continue;
            }
            auto& sourceController =
                require_controller(connection.source.simulator);
            if (!has_active_clock(
                    describe(connection.source),
                    connection.source.simulator,
                    firedOutputs,
                    activatedInputs)) {
                continue;
            }
            if (firedOutputs.count(connection.source) == 0 &&
                !sourceController.get_output_clock(connection.source.reference)) {
                continue;
            }
            firedOutputs.insert(connection.source);
            propagatedConnections.insert(index);
            if (activatedInputs.count(connection.target) == 0) {
                const auto data = capture_clocked_data(
                    connection,
                    firedOutputs,
                    activatedInputs,
                    transferredData,
                    observedData);
                for (const auto& entry : data) {
                    simulators_.at(entry.first.simulator).sim->stage_clocked_input(entry.first.type, entry.first.reference, entry.second);
                }
                activate_input_clock(connection.target);
                activatedInputs.insert(connection.target);
                retract_deactivated_outputs(
                    connection.target.simulator, firedOutputs, activatedInputs,
                    transferredData, propagatedConnections, observedData);
                verify_clocked_data_consistency(
                    connection.target.simulator, firedOutputs, activatedInputs, observedData);
                changed = true;
            }
        }
        return changed;
    }

    bool discover_outputs(
        std::unordered_set<variable_id>& firedOutputs,
        const std::unordered_set<variable_id>& activatedInputs);

    std::vector<std::pair<variable_id, variable_value>> capture_clocked_data(
        const clock_connection& clockConnection,
        const std::unordered_set<variable_id>& firedOutputs,
        const std::unordered_set<variable_id>& activatedInputs,
        std::unordered_set<variable_id>& transferredData,
        std::vector<observed_clocked_data>& observedData);

    static bool has_active_clock(
        const variable_description& variable,
        simulator_index simulator,
        const std::unordered_set<variable_id>& firedOutputs,
        const std::unordered_set<variable_id>& activatedInputs);

    void advance_periodic_clocks(time_point currentTime);

    bool process_event(
        time_point currentTime,
        bool roundAlreadyAcquired = false,
        const std::unordered_set<simulator_index>* participants = nullptr);

    bool process_event_unchecked(time_point currentTime, bool roundAlreadyAcquired);

    time_point start_time_;
    std::optional<time_point> next_event_time_;
    event_iteration_budget* eventBudget_ = nullptr;
    event_value_tolerance eventValueTolerance_ = default_event_value_tolerance;
    std::function<void()> beforeInputActivation_;
    const std::unordered_set<simulator_index>* activeParticipants_ = nullptr;
    time_point activeEventTime_;
    bool terminated_ = false;
    bool recovery_required_ = false;
    std::size_t retractions_ = 0;
    std::unordered_map<simulator_index, simulator_info> simulators_;
    std::vector<clock_connection> clock_connections_;
    std::vector<std::size_t> clock_order_;
    bool clock_order_dirty_ = true;
    std::vector<data_connection> clocked_data_connections_;
    std::vector<periodic_clock> periodic_clocks_;
    std::vector<changing_clock> changing_clocks_;
};

} // namespace cosim

#endif
