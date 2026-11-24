/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef LIBCOSIM_ALGORITHM_EVENT_ROUTER_HPP
#define LIBCOSIM_ALGORITHM_EVENT_ROUTER_HPP

#include "../event_value.hpp"
#include "clock_router.hpp"

#include "cosim/algorithm/simulator.hpp"
#include "cosim/error.hpp"
#include "cosim/event_iteration.hpp"
#include "cosim/exception.hpp"
#include "cosim/execution.hpp"
#include "cosim/function/function.hpp"
#include "cosim/log/logger.hpp"
#include "cosim/model_description.hpp"
#include "cosim/serialization.hpp"
#include "cosim/slave.hpp"
#include "cosim/time.hpp"

#include <boost/algorithm/cxx11/any_of.hpp>
#include <boost/algorithm/cxx11/none_of.hpp>
#include <boost/range/algorithm/find_if.hpp>
#include <boost/range/algorithm/remove_if.hpp>
#include <boost/range/algorithm/sort.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace cosim
{

/**
 * Coordinates ordinary input and output events across connected simulators
 * and functions. Simulator and function pointers are non-owning and must
 * remain valid until removed. Clock events use the same per-time budget and
 * are interleaved with propagation through the clock_router.
 */
class event_router
{
public:
    void set_max_event_iterations(std::size_t limit)
    {
        budget_.set_limit(limit);
    }

    void set_event_value_tolerance(event_value_tolerance tolerance)
    {
        // NOLINTNEXTLINE(readability-simplify-boolean-expr): Preserve input-check diagnostics.
        COSIM_INPUT_CHECK(std::isfinite(tolerance.absolute) && tolerance.absolute >= 0.0);
        // NOLINTNEXTLINE(readability-simplify-boolean-expr): Preserve input-check diagnostics.
        COSIM_INPUT_CHECK(std::isfinite(tolerance.relative) && tolerance.relative >= 0.0);
        valueTolerance_ = tolerance;
    }

    event_value_tolerance value_tolerance() const noexcept { return valueTolerance_; }

    event_iteration_budget& budget() noexcept { return budget_; }
    const event_iteration_budget& budget() const noexcept { return budget_; }
    serialization::node export_state() const { return budget_.export_state(); }
    void import_state(const serialization::node& state) { budget_.import_state(state); }
    void reset_state() { budget_.reset(); }

    struct output_snapshot
    {
        variable_id variable;
        variable_value rawValue;
        variable_value effectiveValue;
    };
    using snapshot = std::vector<output_snapshot>;

    void initialize(time_point time, duration nextStep, clock_router& clocks)
    {
        if (coordinate_due_nonclock_events(time, nextStep, nullptr)) {
            throw error(make_error_code(errc::simulation_error),
                "A simulator requested termination during initialization");
        }
        bool changed = false;
        route_clock_events(clocks, changed, [&] {
            clocks.initialize(time);
            return false;
        });
    }

    bool prepare_clock_step(time_point time, duration nextStep, clock_router& clocks,
        const std::unordered_set<simulator_index>* participants = nullptr)
    {
        if (coordinate_due_nonclock_events(time, nextStep, participants)) {
            return true;
        }
        bool changed = false;
        return route_clock_events(clocks, changed, [&] { return clocks.prepare_step(time, participants); }, participants);
    }

    bool finish_step_events(time_point time, duration nextStep, clock_router& clocks,
        const std::unordered_set<simulator_index>& eventSources,
        const std::unordered_set<simulator_index>* participants = nullptr)
    {
        const auto sources = nonclock_event_sources(time, eventSources, participants);
        if (!sources.empty()) {
            begin_input_coordination(time, participants);
            if (coordinate_nonclock_events(time, nextStep, false, participants, sources)) {
                return true;
            }
        }
        bool inputsChanged = false;
        if (route_clock_events(clocks, inputsChanged, [&] { return clocks.complete_step(time, eventSources, participants); }, participants)) {
            return true;
        }
        if (inputsChanged || !sources.empty()) {
            if (process_inputs(time, nextStep, clocks, false, participants)) {
                return true;
            }
            finish_input_coordination(participants);
        }
        return false;
    }

    void require_step_progress(time_point start, time_point end)
    {
        if (end == start && !budget_.acquire_zero_progress_step(start)) {
            throw error(make_error_code(errc::simulation_error),
                "Simulators repeatedly returned without advancing simulation time");
        }
    }

    bool prepare_step_inputs(
        time_point currentTime,
        duration& stepSize,
        clock_router& clocks,
        const std::unordered_set<simulator_index>* participants = nullptr)
    {
        return process_inputs(currentTime, stepSize, clocks, true, participants) ||
            settle_step_size(currentTime, stepSize, clocks, [&clocks, currentTime](duration size) { return clocks.cap_step(currentTime, size); }, participants);
    }

    /// Registers a non-owning simulator and invalidates the dependency order.
    void add_simulator(simulator_index index, simulator* sim,
        std::shared_ptr<const model_description> description)
    {
        simulators_.emplace(index, sim);
        descriptions_.emplace(index, std::move(description));
        order_.reset();
    }

    /// Registers a non-owning function for event-driven reevaluation.
    void add_function(function_index index, function* fun)
    {
        functions_.emplace(index, fun);
        functionOrder_.reset();
        order_.reset();
    }

    void remove_simulator(simulator_index index)
    {
        simulators_.erase(index);
        descriptions_.erase(index);
        order_.reset();
        connections_.erase(
            boost::range::remove_if(connections_,
                [index](const auto& connection) {
                    return connection.first.simulator == index ||
                        connection.second.simulator == index;
                }),
            connections_.end());
        functionConnections_.erase(
            boost::range::remove_if(functionConnections_,
                [index](const auto& connection) {
                    return connection.first.simulator == index;
                }),
            functionConnections_.end());
        functionSimulatorConnections_.erase(
            boost::range::remove_if(functionSimulatorConnections_,
                [index](const auto& connection) {
                    return connection.second.simulator == index;
                }),
            functionSimulatorConnections_.end());
    }

    void connect_data(variable_id source, variable_id target)
    {
        connections_.emplace_back(source, target);
        order_.reset();
    }

    void connect_function(variable_id source, function_io_id target)
    {
        functionConnections_.emplace_back(source, target);
        order_.reset();
    }

    void connect_function(function_io_id source, variable_id target)
    {
        functionSimulatorConnections_.emplace_back(source, target);
        order_.reset();
    }

    void connect_function(function_io_id source, function_io_id target)
    {
        functionFunctionConnections_.emplace_back(source, target);
        functionOrder_.reset();
        order_.reset();
    }

    void disconnect(variable_id target)
    {
        order_.reset();
        connections_.erase(
            boost::range::remove_if(connections_,
                [target](const auto& connection) { return connection.second == target; }),
            connections_.end());
        functionSimulatorConnections_.erase(
            boost::range::remove_if(functionSimulatorConnections_,
                [target](const auto& connection) { return connection.second == target; }),
            functionSimulatorConnections_.end());
    }

    void disconnect(function_io_id target)
    {
        functionOrder_.reset();
        order_.reset();
        functionConnections_.erase(
            boost::range::remove_if(functionConnections_,
                [target](const auto& connection) { return connection.second == target; }),
            functionConnections_.end());
        functionFunctionConnections_.erase(
            boost::range::remove_if(functionFunctionConnections_,
                [target](const auto& connection) { return connection.second == target; }),
            functionFunctionConnections_.end());
    }

private:
    template<typename Visitor>
    void for_each_reachable_function_target(function_index first, const Visitor& visit) const
    {
        std::vector<function_index> pending{first};
        std::unordered_set<function_index> visited;
        while (!pending.empty()) {
            const auto function = pending.back();
            pending.pop_back();
            if (!visited.insert(function).second) {
                continue;
            }
            for (const auto& target : functionSimulatorConnections_) {
                if (target.first.function == function) {
                    visit(target.second);
                }
            }
            for (const auto& next : functionFunctionConnections_) {
                if (next.first.function == function) {
                    pending.push_back(next.second.function);
                }
            }
        }
    }

    void warn_about_mixed_capability_pair(
        simulator_index eventIndex, simulator_index peerIndex) const
    {
        auto* eventSim = simulators_.at(eventIndex);
        auto* peer = simulators_.at(peerIndex);
        if (eventSim->get_event_controller() == nullptr &&
            !descriptions_.at(eventIndex)->capabilities.might_return_early) {
            return;
        }
        const bool missingEventMode = peer->get_event_controller() == nullptr;
        const bool missingRollback = !descriptions_.at(peerIndex)->capabilities.can_save_state;
        const bool unsafeReplay =
            !missingRollback && !peer->supports_automatic_step_replay();
        if (!missingEventMode && !missingRollback && !unsafeReplay) {
            return;
        }
        BOOST_LOG_SEV(log::logger(), log::warning)
            << eventSim->name() << " is connected to " << peer->name()
            << ", which " << (missingEventMode ? "does not support Event Mode" : "")
            << (missingEventMode && (missingRollback || unsafeReplay) ? " and " : "")
            << (missingRollback ? "cannot save and restore state" : "")
            << (unsafeReplay ? "cannot safely replay a step automatically" : "")
            << "; event delivery may be delayed or cannot be synchronized exactly";
    }

public:
    /// Warns once per connected participant pair about missing event capabilities.
    void warn_about_mixed_capability_connections(const clock_router& clocks) const
    {
        std::set<std::pair<simulator_index, simulator_index>> pairs;
        const auto addPair = [&pairs](simulator_index first, simulator_index second) {
            if (first != second) {
                pairs.emplace(std::min(first, second), std::max(first, second));
            }
        };
        for (const auto& connection : connections_) {
            addPair(connection.first.simulator, connection.second.simulator);
        }
        clocks.for_each_connection(addPair);
        for (const auto& connection : functionConnections_) {
            for_each_reachable_function_target(connection.second.function,
                [&](variable_id target) { addPair(connection.first.simulator, target.simulator); });
        }
        for (const auto& pair : pairs) {
            warn_about_mixed_capability_pair(pair.first, pair.second);
            warn_about_mixed_capability_pair(pair.second, pair.first);
        }
    }

    /// Captures raw and modified connected outputs for later change detection.
    snapshot capture(
        std::optional<simulator_index> source = std::nullopt,
        const std::unordered_set<simulator_index>* participants = nullptr) const
    {
        snapshot result;
        std::unordered_set<variable_id> captured;
        captured.reserve(connections_.size() + functionConnections_.size());
        const auto append = [&](variable_id variable) {
            if (source && variable.simulator != *source) {
                return;
            }
            if (participants && participants->count(variable.simulator) == 0) {
                return;
            }
            if (!captured.insert(variable).second) {
                return;
            }
            const auto* sim = simulators_.at(variable.simulator);
            result.push_back({
                variable,
                sim->get_event_output_value(variable.type, variable.reference),
                sim->get_variable_value(variable.type, variable.reference),
            });
        };
        for (const auto& connection : connections_) {
            append(connection.first);
        }
        for (const auto& connection : functionConnections_) {
            append(connection.first);
        }
        return result;
    }

private:
    struct coupling_history
    {
        std::unordered_map<variable_id, std::deque<variable_value>> inputs;
        std::unordered_map<function_io_id, variable_value> functionOutputs;
    };

    bool propagate_changes(snapshot& baseline,
        const std::unordered_set<simulator_index>* participants,
        coupling_history* history = nullptr,
        std::optional<simulator_index> onlySource = std::nullopt)
    {
        bool eventTargetChanged = false;
        std::unordered_map<function_io_id, variable_value> functionInputs;
        for (auto& old : baseline) {
            const auto source = old.variable;
            if (onlySource && *onlySource != source.simulator) {
                continue;
            }
            const auto* sourceSim = simulators_.at(source.simulator);
            const auto rawValue = sourceSim->get_event_output_value(
                source.type, source.reference);
            if (detail::event_values_equal(rawValue, old.rawValue)) {
                continue;
            }
            old.rawValue = rawValue;
            const auto value = sourceSim->get_variable_value(source.type, source.reference);
            if (detail::event_values_close(value, old.effectiveValue, valueTolerance_)) {
                continue;
            }
            old.effectiveValue = value;

            for (const auto& connection : functionConnections_) {
                if (connection.first != source) {
                    continue;
                }
                functionInputs.insert_or_assign(connection.second, value);
            }
            for (const auto& connection : connections_) {
                if (connection.first != source) {
                    continue;
                }
                eventTargetChanged =
                    stage_simulator_input(connection.second, value, history) || eventTargetChanged;
            }
        }
        eventTargetChanged = propagate_functions(functionInputs, participants, history) || eventTargetChanged;
        return eventTargetChanged;
    }

    std::vector<simulator*> ordered_clock_participants(
        const std::unordered_set<simulator_index>* participants) const
    {
        std::vector<simulator*> result;
        for (const auto index : ordered_simulators()) {
            auto* sim = simulators_.at(index);
            if ((sim->get_event_controller() != nullptr) && (sim->get_clock_controller() != nullptr) &&
                ((participants == nullptr) || (participants->count(index) != 0U))) {
                result.push_back(sim);
            }
        }
        return result;
    }

public:
    /**
     * Runs an operation of the Clock router and propagates the event changes
     * it caused to the connected inputs and functions. If functions feed
     * Clock-associated inputs, the function paths are brought up to date just
     * before each input Clock activates, so that those inputs sample the
     * values of the same communication point. Returns the result of `action`,
     * which tells whether termination was requested. `changed` tells whether
     * event inputs of other simulators changed and need coordination.
     */
    template<typename Action>
    bool route_clock_events(
        clock_router& clockRouter, bool& changed, Action&& action,
        const std::unordered_set<simulator_index>* participants = nullptr)
    {
        return route_clock_events(
            clockRouter, capture(std::nullopt, participants), changed,
            std::forward<Action>(action), participants);
    }

    /** As above, but compares the outputs against the earlier snapshot `before`. */
    template<typename Action>
    bool route_clock_events(
        clock_router& clockRouter, snapshot before, bool& changed, Action&& action,
        const std::unordered_set<simulator_index>* participants = nullptr)
    {
        changed = false;
        struct hook_guard
        {
            explicit hook_guard(clock_router& clockRouter)
                : router(clockRouter)
            { }
            hook_guard(const hook_guard&) = delete;
            hook_guard& operator=(const hook_guard&) = delete;
            hook_guard(hook_guard&&) = delete;
            hook_guard& operator=(hook_guard&&) = delete;
            ~hook_guard() { router.set_before_input_activation({}); }
            clock_router& router;
        };
        std::optional<hook_guard> guard;
        if (feeds_clocked_inputs_through_functions()) {
            clockRouter.set_before_input_activation([this, &before, &changed, participants] {
                changed = propagate_changes(before, participants) || changed;
                changed = refresh_clock_function_sources(participants) || changed;
                before = capture(std::nullopt, participants);
            });
            guard.emplace(clockRouter);
        }
        const bool terminated = std::forward<Action>(action)();
        guard.reset();
        if (!terminated) {
            changed = propagate_changes(before, participants) || changed;
        }
        return terminated;
    }

    /**
     * Applies the step size limits to `stepSize` after input events were
     * coordinated. Input modifiers were evaluated for the earlier, longer
     * step, so replay-safe ones are evaluated again for the shortened step.
     * The new values can cause further input events, which can shorten the
     * step again; this repeats until the step size is stable. Returns true if
     * the simulation was requested to terminate.
     */
    template<typename CappedStepSize>
    bool settle_step_size(
        time_point currentTime,
        duration& stepSize,
        clock_router& clockRouter,
        const CappedStepSize& cappedStepSize,
        const std::unordered_set<simulator_index>* participants = nullptr)
    {
        for (std::size_t round = 0; round < budget_.limit(); ++round) {
            const auto capped = cappedStepSize(stepSize);
            if (capped == stepSize) {
                return false;
            }
            stepSize = capped;
            bool reevaluated = false;
            for (const auto& entry : simulators_) {
                if (entry.second->get_event_controller() &&
                    entry.second->has_variable_modifiers() &&
                    (!participants || participants->count(entry.first) != 0)) {
                    reevaluated = entry.second->reevaluate_input_modifiers(stepSize) || reevaluated;
                }
            }
            if (!reevaluated) {
                return false;
            }
            if (process_inputs(currentTime, stepSize, clockRouter, true, participants)) {
                return true;
            }
        }
        throw error(make_error_code(errc::simulation_error),
            "The step size did not settle after input events at time " +
                std::to_string(to_double_time_point(currentTime)) + " s");
    }

private:
    bool coordinate_due_nonclock_events(time_point time, duration nextStep,
        const std::unordered_set<simulator_index>* participants)
    {
        const auto sources = nonclock_event_sources(time, {}, participants);
        if (sources.empty()) {
            return false;
        }
        begin_input_coordination(time, participants);
        if (coordinate_nonclock_events(time, nextStep, false, participants, sources)) {
            return true;
        }
        finish_input_coordination(participants, false);
        return false;
    }

    void begin_input_coordination(time_point time,
        const std::unordered_set<simulator_index>* participants)
    {
        budget_.begin(time);
        for (const auto& [index, sim] : simulators_) {
            if ((sim->get_event_controller() != nullptr) && ((participants == nullptr) || (participants->count(index) != 0U))) {
                sim->begin_input_event_coordination(time);
            }
        }
    }

    void finish_input_coordination(const std::unordered_set<simulator_index>* participants,
        bool includeClockParticipants = true)
    {
        for (const auto& entry : simulators_) {
            if ((entry.second->get_event_controller() != nullptr) &&
                (includeClockParticipants || (entry.second->get_clock_controller() == nullptr)) &&
                ((participants == nullptr) || participants->count(entry.first) != 0)) {
                entry.second->finish_input_event_coordination();
            }
        }
    }

    std::unordered_set<simulator_index> nonclock_event_sources(
        time_point time, const std::unordered_set<simulator_index>& requested,
        const std::unordered_set<simulator_index>* participants) const
    {
        std::unordered_set<simulator_index> sources;
        for (const auto& [index, sim] : simulators_) {
            if ((sim->get_event_controller() == nullptr) || (sim->get_clock_controller() != nullptr) ||
                ((participants != nullptr) && participants->count(index) == 0)) {
                continue;
            }
            const auto next = sim->get_event_handling_status().next_event_time;
            if ((requested.count(index) != 0U) || (next && *next <= time)) {
                sources.insert(index);
            }
        }
        return sources;
    }

public:
    /**
     * Coordinates input events at `currentTime`. When `participants` is given,
     * only those simulators take part; the others are not at `currentTime`
     * and keep their staged inputs for their own next communication point.
     */
    bool process_inputs(
        time_point currentTime,
        duration deltaT,
        clock_router& clockRouter,
        bool evaluateUnstagedModifiers = true,
        const std::unordered_set<simulator_index>* participants = nullptr)
    {
        begin_input_coordination(currentTime, participants);
        const auto clockParticipants = ordered_clock_participants(participants);
        while (true) {
            if (coordinate_nonclock_events(currentTime, deltaT, evaluateUnstagedModifiers,
                    participants, {})) {
                return true;
            }
            std::vector<simulator*> pending;
            for (auto* sim : clockParticipants) {
                if (sim->prepare_input_events(deltaT, evaluateUnstagedModifiers)) {
                    pending.push_back(sim);
                }
            }
            if (pending.empty()) {
                return false;
            }
            if (!budget_.acquire(currentTime)) {
                budget_.warn(currentTime);
                for (auto* sim : pending) {
                    sim->defer_input_events(currentTime);
                }
                return false;
            }
            clockRouter.check_input_event_supported(currentTime);
            clockRouter.require_clock_participants(participants);
            const auto before = capture(std::nullopt, participants);
            for (auto* sim : clockParticipants) {
                auto* controller = sim->get_event_controller();
                if (!controller->is_in_event_mode()) {
                    controller->enter_event_mode();
                }
                sim->apply_event_inputs();
            }
            bool changed = false;
            if (route_clock_events(clockRouter, before, changed, [&] { return clockRouter.process_input_event(currentTime, participants); }, participants)) {
                return true;
            }
        }
    }

private:
    struct nonclock_event_iteration
    {
        event_router& router; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members): The iteration borrows its router.
        time_point time;
        duration deltaT;
        bool evaluateUnstagedModifiers;
        const std::unordered_set<simulator_index>* participants;
        std::vector<simulator_index> eligible;
        std::unordered_set<simulator_index> pending;
        std::unordered_set<simulator_index> active;
        std::unordered_set<simulator_index> unfinished;
        std::unordered_set<simulator_index> needsRefresh;
        std::unordered_map<simulator_index, std::size_t> consecutiveUpdates;
        snapshot baseline;
        coupling_history history;

        nonclock_event_iteration(event_router& owner, time_point currentTime,
            duration stepSize, bool evaluateModifiers,
            const std::unordered_set<simulator_index>* selected)
            : router(owner)
            , time(currentTime)
            , deltaT(stepSize)
            , evaluateUnstagedModifiers(evaluateModifiers)
            , participants(selected)
        {
            for (const auto index : router.ordered_simulators()) {
                auto* sim = router.simulators_.at(index);
                if ((sim->get_event_controller() != nullptr) && (sim->get_clock_controller() == nullptr) &&
                    ((participants == nullptr) || (participants->count(index) != 0U))) {
                    eligible.push_back(index);
                }
            }
        }

        void prepare(simulator_index index)
        {
            if (router.simulators_.at(index)->prepare_input_events(deltaT, evaluateUnstagedModifiers)) {
                pending.insert(index);
            } else {
                pending.erase(index);
            }
        }

        void join(simulator_index index)
        {
            if (!active.insert(index).second) {
                return;
            }
            auto* controller = router.simulators_.at(index)->get_event_controller();
            if (!controller->is_in_event_mode()) {
                controller->enter_event_mode();
            }
            unfinished.insert(index);
            needsRefresh.insert(index);
        }

        bool update(simulator_index index)
        {
            auto* sim = router.simulators_.at(index);
            auto& count = consecutiveUpdates[index];
            if (count == router.budget_.limit()) {
                throw error(make_error_code(errc::simulation_error),
                    sim->name() + ": input event did not converge within the iteration limit");
            }
            ++count;
            const auto result = sim->get_event_controller()->update_event_state();
            if (result.discrete_states_need_update ||
                (result.next_event_time && *result.next_event_time <= time)) {
                unfinished.insert(index);
            } else {
                unfinished.erase(index);
                count = 0;
            }
            return result.terminate_simulation;
        }

        bool initialize(const std::unordered_set<simulator_index>& sources)
        {
            for (auto const index : eligible) {
                prepare(index);
            }
            if (pending.empty() && sources.empty()) {
                return false;
            }
            baseline = router.capture(std::nullopt, participants);
            for (auto const index : eligible) {
                if (sources.count(index) != 0U) {
                    join(index);
                }
            }
            return true;
        }

        void sweep_inputs()
        {
            for (auto index : eligible) {
                auto* sim = router.simulators_.at(index);
                prepare(index);
                const bool applicable = (pending.count(index) != 0U) && sim->has_applicable_event_inputs();
                if (!applicable && (needsRefresh.count(index) == 0U)) {
                    continue;
                }
                join(index);
                if (applicable) {
                    sim->apply_coupled_event_inputs();
                }
                sim->refresh_event_values();
                needsRefresh.erase(index);
                router.propagate_changes(baseline, participants, &history, index);
            }
        }

        bool can_apply_inputs()
        {
            bool canApply = false;
            for (auto const index : eligible) {
                prepare(index);
                if ((pending.count(index) != 0U) && router.simulators_.at(index)->has_applicable_event_inputs()) {
                    canApply = true;
                }
            }
            return canApply;
        }

        bool sweep_until_settled(std::size_t instant)
        {
            for (auto const index : eligible) {
                if (pending.count(index) != 0U) {
                    join(index);
                }
            }
            for (std::size_t sweep = 0;; ++sweep) {
                sweep_inputs();
                if (!can_apply_inputs()) {
                    return true;
                }
                if (sweep + 1 == router.budget_.limit()) {
                    router.warn_nonconvergence(time, instant, true, eligible, unfinished, pending, history);
                    // This instant has applied inputs but has not committed them yet.
                    unfinished.insert(active.begin(), active.end());
                    return false;
                }
            }
        }

        bool commit_round()
        {
            for (auto const index : eligible) {
                if ((active.count(index) != 0U) && update(index)) {
                    return true;
                }
            }
            // Publish only after every participant has committed this instant.
            for (auto const index : eligible) {
                if (active.count(index) != 0U) {
                    router.simulators_.at(index)->refresh_event_values();
                }
            }
            router.propagate_changes(baseline, participants, &history);
            for (auto const index : eligible) {
                prepare(index);
            }
            return false;
        }

        bool finish_exhausted()
        {
            for (auto const index : eligible) {
                while (unfinished.count(index) != 0U) {
                    if (update(index)) {
                        return true;
                    }
                }
                if (active.count(index) != 0U) {
                    router.simulators_.at(index)->refresh_event_values();
                }
            }
            router.propagate_changes(baseline, participants, &history);
            for (auto const index : eligible) {
                prepare(index);
                if (pending.count(index) != 0U) {
                    router.simulators_.at(index)->defer_input_events(time);
                }
            }
            return false;
        }

        void enter_step_mode()
        {
            for (auto const index : eligible) {
                if (active.count(index) != 0U) {
                    router.simulators_.at(index)->get_event_controller()->enter_step_mode();
                }
            }
        }
    };

    bool coordinate_nonclock_events(time_point time, duration deltaT, bool evaluateUnstagedModifiers,
        const std::unordered_set<simulator_index>* participants,
        const std::unordered_set<simulator_index>& sources)
    {
        nonclock_event_iteration iteration(*this, time, deltaT, evaluateUnstagedModifiers, participants);
        if (!iteration.initialize(sources)) {
            return false;
        }
        bool exhausted = false;
        while (!iteration.pending.empty() || !iteration.unfinished.empty()) {
            const auto instant = budget_.rounds_used();
            if (!budget_.acquire(time)) {
                warn_nonconvergence(time, instant, false, iteration.eligible,
                    iteration.unfinished, iteration.pending, iteration.history);
                exhausted = true;
                break;
            }
            if (!iteration.sweep_until_settled(instant)) {
                exhausted = true;
                break;
            }
            if (iteration.commit_round()) {
                return true;
            }
        }
        if (exhausted && iteration.finish_exhausted()) {
            return true;
        }
        iteration.enter_step_mode();
        return false;
    }

    static void append_sample(std::ostringstream& entry, const variable_value& sample)
    {
        std::visit([&](const auto& data) {
            if (data.values.size() != 1) {
                entry << '[';
            }
            const auto count = std::min<std::size_t>(data.values.size(), 4);
            for (std::size_t i = 0; i < count; ++i) {
                if (i) {
                    entry << ',';
                }
                using element = typename std::decay_t<decltype(data.values)>::value_type;
                if constexpr (std::is_integral_v<element>) {
                    entry << +data.values.at(i);
                } else {
                    entry << data.values.at(i);
                }
            }
            if (data.values.size() > count) {
                entry << ",...";
            }
            if (data.values.size() != 1) {
                entry << ']';
            }
        },
            sample.values());
    }

    void warn_nonconvergence(time_point time, std::size_t instant, bool innerLimit,
        const std::vector<simulator_index>& order,
        const std::unordered_set<simulator_index>& unfinished,
        const std::unordered_set<simulator_index>& pending,
        const coupling_history& history)
    {
        std::ostringstream details;
        details << "; super-dense index " << instant
                << (innerLimit ? " (coupling sweep limit)" : " (coupled round limit)")
                << "; FMUs with unfinished updates:";
        for (auto const index : order) {
            if (unfinished.count(index) != 0U) {
                details << ' ' << simulators_.at(index)->name();
            }
        }
        details << "; pending inputs at:";
        for (auto const index : order) {
            if (pending.count(index) != 0U) {
                details << ' ' << simulators_.at(index)->name();
            }
        }
        std::vector<std::string> changes;
        for (const auto& [input, samples] : history.inputs) {
            if (samples.size() < 2) {
                continue;
            }
            std::ostringstream entry;
            entry << simulators_.at(input.simulator)->name() << '.' << describe(input).name << '=';
            const char* separator = "";
            for (const auto& sample : samples) {
                entry << separator;
                separator = " -> ";
                append_sample(entry, sample);
            }
            changes.push_back(entry.str());
        }
        boost::range::sort(changes);
        if (!changes.empty()) {
            details << "; recent input values:";
            for (const auto& change : changes) {
                details << ' ' << change;
            }
        }
        for (const auto& cycle : dependencyCycles_) {
            if (boost::algorithm::none_of(cycle, [&](simulator_index index) {
                    return pending.count(index) || unfinished.count(index);
                })) {
                continue;
            }
            details << "; dependency cycle:";
            for (auto const index : cycle) {
                details << ' ' << simulators_.at(index)->name();
            }
        }
        budget_.warn(time, details.str());
    }

    const variable_description& describe(variable_id identifier) const
    {
        const auto& variables = descriptions_.at(identifier.simulator)->variables;
        const auto variable = boost::range::find_if(
            variables, [identifier](const auto& item) {
                return item.type == identifier.type && item.reference == identifier.reference;
            });
        if (variable == variables.end()) {
            throw error(make_error_code(errc::invalid_system_structure),
                "An event connection references an unknown variable");
        }
        return *variable;
    }

    bool feeds_clocked_inputs_through_functions() const
    {
        return boost::algorithm::any_of(
            functionSimulatorConnections_,
            [this](const auto& connection) {
                return !describe(connection.second).clocks.empty();
            });
    }

    bool refresh_clock_function_sources(
        const std::unordered_set<simulator_index>* participants)
    {
        std::unordered_set<function_index> relevant;
        std::vector<function_index> pending;
        for (const auto& connection : functionSimulatorConnections_) {
            if (!describe(connection.second).clocks.empty()) {
                pending.push_back(connection.first.function);
            }
        }
        while (!pending.empty()) {
            const auto function = pending.back();
            pending.pop_back();
            if (!relevant.insert(function).second) {
                continue;
            }
            for (const auto& connection : functionFunctionConnections_) {
                if (connection.second.function == function) {
                    pending.push_back(connection.first.function);
                }
            }
        }
        std::unordered_map<function_io_id, variable_value> inputs;
        for (const auto& connection : functionConnections_) {
            if (relevant.count(connection.second.function) == 0 ||
                ((participants != nullptr) && participants->count(connection.first.simulator) == 0) ||
                !describe(connection.first).clocks.empty()) {
                continue;
            }
            const auto value = simulators_.at(connection.first.simulator)
                                   ->get_variable_value(connection.first.type, connection.first.reference);
            if (!detail::event_values_close(
                    value, functions_.at(connection.second.function)->get_variable_value(connection.second.reference), valueTolerance_)) {
                inputs.insert_or_assign(connection.second, value);
            }
        }
        return propagate_functions(inputs, participants);
    }

    bool stage_simulator_input(variable_id input, const variable_value& value,
        coupling_history* history)
    {
        auto* target = simulators_.at(input.simulator);
        const auto& variable = describe(input);
        if (!variable.clocks.empty()) {
            // A Clock-associated input is sampled when its Clock activates, so
            // the value is held and no event iteration is needed for it.
            target->set_variable_value(input.type, input.reference, value);
            return false;
        }
        const bool hasEventController = target->get_event_controller() != nullptr;
        if (hasEventController && (history != nullptr)) {
            auto& samples = history->inputs[input];
            if (samples.empty() || !detail::event_values_equal(samples.back(), value)) {
                if (samples.size() == 4) {
                    samples.pop_front();
                }
                samples.push_back(value);
            }
        }
        if (hasEventController && variable.causality == variable_causality::input &&
            variable.variability == variable_variability::continuous) {
            target->signal_input_discontinuity(input.type, input.reference);
        }
        target->set_variable_value(input.type, input.reference, value);
        return hasEventController;
    }

    const std::vector<function_index>& ordered_functions()
    {
        if (functionOrder_) {
            return *functionOrder_;
        }
        std::map<function_index, std::size_t> indegrees;
        for (const auto& entry : functions_) {
            indegrees.emplace(entry.first, 0);
        }
        std::unordered_map<function_index, std::vector<function_index>> edges;
        for (const auto& connection : functionFunctionConnections_) {
            ++indegrees.at(connection.second.function);
            edges[connection.first.function].push_back(connection.second.function);
        }
        std::set<function_index> ready;
        for (const auto& entry : indegrees) {
            if (entry.second == 0) {
                ready.insert(entry.first);
            }
        }
        std::vector<function_index> order;
        while (!ready.empty()) {
            const auto index = *ready.begin();
            ready.erase(ready.begin());
            order.push_back(index);
            const auto outgoing = edges.find(index);
            if (outgoing == edges.end()) {
                continue;
            }
            for (const auto target : outgoing->second) {
                if (--indegrees.at(target) == 0) {
                    ready.insert(target);
                }
            }
        }
        if (order.size() != functions_.size()) {
            throw error(make_error_code(errc::unsupported_feature),
                "Cyclic function-to-function event connections are not supported");
        }
        functionOrder_ = std::move(order);
        return *functionOrder_;
    }

    using function_values = std::unordered_map<function_io_id, variable_value>;

    void stage_function_sources(function_index index, function* fun,
        function_values& pendingInputs,
        const std::unordered_set<simulator_index>* participants)
    {
        for (const auto& connection : functionConnections_) {
            if (connection.second.function != index ||
                pendingInputs.count(connection.second) != 0) {
                continue;
            }
            if ((participants != nullptr) && participants->count(connection.first.simulator) == 0) {
                continue;
            }
            const auto value = simulators_.at(connection.first.simulator)
                                   ->get_variable_value(connection.first.type, connection.first.reference);
            if (!detail::event_values_close(
                    value, fun->get_variable_value(connection.second.reference), valueTolerance_)) {
                pendingInputs.emplace(connection.second, value);
            }
        }
    }

    function_values capture_function_outputs(
        function_index index, function* fun, coupling_history* history)
    {
        function_values oldOutputs;
        const auto remember = [&](function_io_id output) {
            if (output.function == index && oldOutputs.count(output) == 0) {
                auto value = fun->get_variable_value(output.reference);
                if (history) {
                    value = history->functionOutputs.try_emplace(output, value).first->second;
                }
                oldOutputs.emplace(output, std::move(value));
            }
        };
        for (const auto& connection : functionSimulatorConnections_) {
            remember(connection.first);
        }
        for (const auto& connection : functionFunctionConnections_) {
            remember(connection.first);
        }
        return oldOutputs;
    }

    static void apply_function_inputs(function_index index, function* fun,
        function_values& pendingInputs)
    {
        for (auto it = pendingInputs.begin(); it != pendingInputs.end();) {
            if (it->first.function == index) {
                fun->set_variable_value(it->first.reference, it->second);
                it = pendingInputs.erase(it);
            } else {
                ++it;
            }
        }
    }

    bool publish_function_outputs(function* fun, const function_values& oldOutputs,
        function_values& pendingInputs, coupling_history* history)
    {
        bool eventTargetChanged = false;
        for (const auto& output : oldOutputs) {
            const auto value = fun->get_variable_value(output.first.reference);
            if (detail::event_values_close(value, output.second, valueTolerance_)) {
                continue;
            }
            if (history != nullptr) {
                history->functionOutputs.insert_or_assign(output.first, value);
            }
            for (const auto& connection : functionSimulatorConnections_) {
                if (connection.first == output.first) {
                    eventTargetChanged =
                        stage_simulator_input(connection.second, value, history) || eventTargetChanged;
                }
            }
            for (const auto& connection : functionFunctionConnections_) {
                if (connection.first == output.first) {
                    pendingInputs.insert_or_assign(connection.second, value);
                }
            }
        }
        return eventTargetChanged;
    }

    bool propagate_functions(
        function_values& pendingInputs,
        const std::unordered_set<simulator_index>* participants,
        coupling_history* history = nullptr)
    {
        if (pendingInputs.empty()) {
            return false;
        }
        bool eventTargetChanged = false;
        for (const auto index : ordered_functions()) {
            const auto affected = boost::algorithm::any_of(
                pendingInputs,
                [index](const auto& entry) { return entry.first.function == index; });
            if (!affected) {
                continue;
            }
            auto* fun = functions_.at(index);
            if (!fun->supports_event_iteration()) {
                throw error(make_error_code(errc::unsupported_feature),
                    "A function in an event path does not support repeated evaluation");
            }
            stage_function_sources(index, fun, pendingInputs, participants);
            const auto oldOutputs = capture_function_outputs(index, fun, history);
            apply_function_inputs(index, fun, pendingInputs);
            fun->calculate();
            eventTargetChanged =
                publish_function_outputs(fun, oldOutputs, pendingInputs, history) || eventTargetChanged;
        }
        return eventTargetChanged;
    }

    using dependency_edges = std::map<simulator_index, std::set<simulator_index>>;

    void add_dependency_edge(
        dependency_edges& edges, simulator_index source, variable_id input) const
    {
        const auto target = input.simulator;
        bool hasOutputs = false;
        bool affected = false;
        const auto& variables = descriptions_.at(target)->variables;
        for (const auto& downstream : connections_) {
            if (downstream.first.simulator != target) {
                continue;
            }
            hasOutputs = true;
            const auto output = boost::range::find_if(
                variables, [&downstream](const auto& variable) {
                    return variable.type == downstream.first.type &&
                        variable.reference == downstream.first.reference;
                });
            if (output == variables.end() || !output->output_dependencies) {
                affected = true;
                break;
            }
            const typed_value_reference typedInput{
                input.type,
                input.reference,
            };
            if (std::any_of(output->output_dependencies->begin(), // NOLINT(bugprone-unchecked-optional-access): Missing dependencies were handled above.
                    output->output_dependencies->end(), // NOLINT(bugprone-unchecked-optional-access): Missing dependencies were handled above.
                    [typedInput](const auto& dependency) {
                        return dependency.variable == typedInput;
                    })) {
                affected = true;
                break;
            }
        }
        if (boost::algorithm::any_of(functionConnections_,
                [target](const auto& connection) { return connection.first.simulator == target; })) {
            affected = true;
        }
        if (affected || !hasOutputs) {
            edges.at(source).insert(target);
        }
    }

    static std::vector<std::vector<simulator_index>> dependency_components(const dependency_edges& edges)
    {
        std::map<simulator_index, int> indices;
        std::map<simulator_index, int> lowLinks;
        std::set<simulator_index> onStack;
        std::vector<simulator_index> stack;
        std::vector<std::vector<simulator_index>> components;
        int nextIndex = 0;
        std::function<void(simulator_index)> visit = [&](simulator_index node) {
            indices[node] = lowLinks[node] = nextIndex++;
            stack.push_back(node);
            onStack.insert(node);
            for (const auto next : edges.at(node)) {
                if (indices.count(next) == 0) {
                    visit(next);
                    lowLinks[node] = std::min(lowLinks[node], lowLinks[next]);
                } else if (onStack.count(next) != 0) {
                    lowLinks[node] = std::min(lowLinks[node], indices[next]);
                }
            }
            if (lowLinks[node] != indices[node]) {
                return;
            }
            components.emplace_back();
            while (true) {
                const auto member = stack.back();
                stack.pop_back();
                onStack.erase(member);
                components.back().push_back(member);
                if (member == node) {
                    break;
                }
            }
            std::sort(components.back().begin(), components.back().end());
        };
        for (const auto& entry : edges) {
            if (indices.count(entry.first) == 0) {
                visit(entry.first);
            }
        }
        return components;
    }

    static std::vector<simulator_index> order_dependency_components(
        const dependency_edges& edges,
        const std::vector<std::vector<simulator_index>>& components,
        std::size_t simulatorCount)
    {
        std::map<simulator_index, std::size_t> componentOf;
        for (std::size_t i = 0; i < components.size(); ++i) {
            for (const auto member : components.at(i)) {
                componentOf.emplace(member, i);
            }
        }
        std::vector<std::set<std::size_t>> componentEdges(components.size());
        std::vector<std::size_t> indegrees(components.size(), 0);
        for (const auto& entry : edges) {
            for (const auto target : entry.second) {
                const auto from = componentOf.at(entry.first);
                const auto destination = componentOf.at(target);
                if (from != destination && componentEdges.at(from).insert(destination).second) {
                    ++indegrees.at(destination);
                }
            }
        }
        std::set<std::pair<simulator_index, std::size_t>> ready;
        for (std::size_t i = 0; i < components.size(); ++i) {
            if (indegrees.at(i) == 0) {
                ready.emplace(components.at(i).front(), i);
            }
        }
        std::vector<simulator_index> order;
        order.reserve(simulatorCount);
        while (!ready.empty()) {
            const auto component = ready.begin()->second;
            ready.erase(ready.begin());
            const auto& members = components.at(component);
            order.insert(order.end(), members.begin(), members.end());
            for (const auto target : componentEdges.at(component)) {
                if (--indegrees.at(target) == 0) {
                    ready.emplace(components.at(target).front(), target);
                }
            }
        }
        return order;
    }

    const std::vector<simulator_index>& ordered_simulators() const
    {
        if (order_) {
            return *order_;
        }
        // Dependencies only guide ordering; actual output changes are always
        // propagated and may requeue an earlier component.
        dependency_edges edges;
        for (const auto& entry : simulators_) {
            edges.emplace(entry.first, std::set<simulator_index>{});
        }
        for (const auto& connection : connections_) {
            add_dependency_edge(edges, connection.first.simulator, connection.second);
        }
        for (const auto& connection : functionConnections_) {
            for_each_reachable_function_target(connection.second.function,
                [&](variable_id target) { add_dependency_edge(edges, connection.first.simulator, target); });
        }

        const auto components = dependency_components(edges);
        dependencyCycles_.clear();
        for (const auto& component : components) {
            if (component.size() > 1 || (edges.at(component.front()).count(component.front()) != 0U)) {
                dependencyCycles_.push_back(component);
            }
        }
        auto order = order_dependency_components(edges, components, simulators_.size());
        order_ = std::move(order);
        return *order_;
    }

    std::unordered_map<simulator_index, simulator*> simulators_;
    std::unordered_map<function_index, function*> functions_;
    std::unordered_map<simulator_index, std::shared_ptr<const model_description>> descriptions_;
    mutable std::optional<std::vector<simulator_index>> order_;
    mutable std::vector<std::vector<simulator_index>> dependencyCycles_;
    std::optional<std::vector<function_index>> functionOrder_;
    event_iteration_budget budget_;
    event_value_tolerance valueTolerance_ = default_event_value_tolerance;
    std::vector<std::pair<variable_id, variable_id>> connections_;
    std::vector<std::pair<variable_id, function_io_id>> functionConnections_;
    std::vector<std::pair<function_io_id, variable_id>> functionSimulatorConnections_;
    std::vector<std::pair<function_io_id, function_io_id>> functionFunctionConnections_;
};

} // namespace cosim

#endif
