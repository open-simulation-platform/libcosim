/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "clock_router.hpp"

#include "../event_value.hpp"

#include "cosim/algorithm/simulator.hpp"
#include "cosim/exception.hpp"
#include "cosim/execution.hpp"
#include "cosim/log/logger.hpp"
#include "cosim/model_description.hpp"
#include "cosim/slave.hpp"
#include "cosim/time.hpp"

#include <boost/algorithm/cxx11/any_of.hpp>
#include <boost/log/sources/severity_feature.hpp>
#include <boost/range/algorithm/find.hpp>
#include <boost/range/algorithm/find_if.hpp>
#include <boost/range/algorithm/remove_if.hpp>

#include <algorithm>
#include <cstddef>
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

namespace
{

std::pair<const variable_description*, bool> sole_input_clock(
    const std::vector<variable_description>& variables)
{
    const variable_description* inputClock = nullptr;
    for (const auto& variable : variables) {
        if (variable.type != variable_type::clock ||
            variable.causality != variable_causality::input) {
            continue;
        }
        if (inputClock != nullptr) {
            return {nullptr, true};
        }
        inputClock = &variable;
    }
    return {inputClock, false};
}

} // namespace

void clock_router::set_before_input_activation(std::function<void()> hook)
{
    beforeInputActivation_ = std::move(hook);
}

void clock_router::check_recovery() const
{
    if (recovery_required_) {
        throw error(make_error_code(errc::simulation_error),
            "Clock event processing failed; restore an earlier execution state or recreate the execution");
    }
}

void clock_router::add_simulator(simulator_index index, simulator* sim,
    std::shared_ptr<const model_description> description)
{
    if (simulators_.count(index) != 0) {
        throw error(
            make_error_code(errc::invalid_system_structure),
            "A simulator was added to the Clock router more than once");
    }
    simulator_info info;
    info.sim = sim;
    info.description = description ? std::move(description)
                                   : std::make_shared<const model_description>(sim->model_description());
    info.controller = sim->get_clock_controller();
    info.has_clock_variables = std::any_of(
        info.description->variables.begin(), info.description->variables.end(),
        [](const auto& variable) { return variable.type == variable_type::clock; });
    simulators_.emplace(index, std::move(info));
    clock_order_dirty_ = true;
}

void clock_router::remove_simulator(simulator_index index)
{
    simulators_.erase(index);
    clock_order_dirty_ = true;
    clock_connections_.erase(
        boost::range::remove_if(
            clock_connections_,
            [index](const auto& connection) {
                return connection.source.simulator == index ||
                    connection.target.simulator == index;
            }),
        clock_connections_.end());
    clocked_data_connections_.erase(
        boost::range::remove_if(
            clocked_data_connections_,
            [index](const auto& connection) {
                return connection.source.simulator == index ||
                    connection.target.simulator == index;
            }),
        clocked_data_connections_.end());
    periodic_clocks_.erase(
        boost::range::remove_if(
            periodic_clocks_,
            [index](const auto& clock) {
                return clock.id.simulator == index;
            }),
        periodic_clocks_.end());
    changing_clocks_.erase(
        boost::range::remove_if(
            changing_clocks_,
            [index](const auto& clock) { return clock.id.simulator == index; }),
        changing_clocks_.end());
}

bool clock_router::connect_data(variable_id source, variable_id target)
{
    const auto& sourceDescription = describe(source);
    const auto& targetDescription = describe(target);
    const bool clocked =
        !sourceDescription.clocks.empty() || !targetDescription.clocks.empty();
    if (!clocked) {
        return false;
    }
    if (!sourceDescription.clocks.empty()) {
        require_controller(source.simulator);
    }
    if (!targetDescription.clocks.empty()) {
        require_controller(target.simulator);
    }
    clocked_data_connections_.push_back({source, target});
    return true;
}

void clock_router::connect_clock(variable_id source, variable_id target)
{
    const auto& sourceDescription = describe(source);
    const auto& targetDescription = describe(target);
    if (source.type != variable_type::clock ||
        target.type != variable_type::clock) {
        throw error(
            make_error_code(errc::invalid_system_structure),
            "Clock connections require Clock variables at both endpoints");
    }
    if (sourceDescription.causality != variable_causality::output ||
        targetDescription.causality != variable_causality::input) {
        throw error(
            make_error_code(errc::invalid_system_structure),
            "Clock connections require an output source and input target");
    }
    if (!sourceDescription.clock || !targetDescription.clock ||
        sourceDescription.clock->interval_variability !=
            clock_interval_variability::triggered ||
        targetDescription.clock->interval_variability !=
            clock_interval_variability::triggered) {
        throw error(
            make_error_code(errc::invalid_system_structure),
            "Only triggered Clock variables may be connected");
    }
    require_controller(source.simulator);
    require_controller(target.simulator);
    if (boost::algorithm::any_of(
            clock_connections_,
            [target](const auto& connection) {
                return connection.target == target;
            })) {
        throw error(
            make_error_code(errc::invalid_system_structure),
            "A triggered input Clock cannot have multiple sources");
    }
    clock_connections_.push_back({source, target});
    clock_order_dirty_ = true;
}

bool clock_router::disconnect(variable_id input)
{
    bool removed = false;
    const auto oldClockCount = clock_connections_.size();
    clock_connections_.erase(
        boost::range::remove_if(
            clock_connections_,
            [input](const auto& connection) {
                return connection.target == input;
            }),
        clock_connections_.end());
    removed = removed || oldClockCount != clock_connections_.size();
    if (removed) {
        clock_order_dirty_ = true;
    }

    const auto oldDataCount = clocked_data_connections_.size();
    clocked_data_connections_.erase(
        boost::range::remove_if(
            clocked_data_connections_,
            [input](const auto& connection) {
                return connection.target == input;
            }),
        clocked_data_connections_.end());
    return removed || oldDataCount != clocked_data_connections_.size();
}

bool clock_router::has_clock_support() const
{
    if (!clock_connections_.empty() ||
        !clocked_data_connections_.empty() ||
        !periodic_clocks_.empty() || !changing_clocks_.empty()) {
        return true;
    }
    return boost::algorithm::any_of(
        simulators_,
        [](const auto& entry) { return entry.second.has_clock_variables; });
}

void clock_router::stage_current_unclocked_data(
    const std::unordered_set<simulator_index>& participants)
{
    for (const auto& connection : clocked_data_connections_) {
        if (participants.count(connection.source.simulator) == 0 ||
            !describe(connection.source).clocks.empty() ||
            describe(connection.target).clocks.empty()) {
            continue;
        }
        const auto value = simulators_.at(connection.source.simulator).sim->get_variable_value(connection.source.type, connection.source.reference);
        simulators_.at(connection.target.simulator).sim->stage_clocked_input(connection.target.type, connection.target.reference, value);
    }
}

bool clock_router::prepare_step(
    time_point currentTime,
    const std::unordered_set<simulator_index>* participants)
{
    check_recovery();
    if (terminated_) {
        throw error(
            make_error_code(errc::simulation_error),
            "The Clock router was used after termination was requested");
    }
    if (process_legacy_events(currentTime, {}, participants)) {
        terminated_ = true;
        return true;
    }
    if (due_clock_model_event(currentTime) ||
        (next_event_time_ && *next_event_time_ <= currentTime)) {
        require_clock_participants(participants);
        return process_event(currentTime, false, participants);
    }
    return false;
}

bool clock_router::complete_step(
    time_point currentTime,
    const std::unordered_set<simulator_index>& eventSources,
    const std::unordered_set<simulator_index>* participants)
{
    check_recovery();
    if (terminated_) {
        return true;
    }
    if (process_legacy_events(currentTime, eventSources, participants)) {
        terminated_ = true;
        return true;
    }
    const bool clockEventRequested = boost::algorithm::any_of(
        eventSources, [this](simulator_index index) {
            return simulators_.at(index).controller != nullptr;
        });
    if (clockEventRequested || due_clock_model_event(currentTime) ||
        (next_event_time_ && *next_event_time_ <= currentTime)) {
        require_clock_participants(participants);
        return process_event(currentTime, false, participants);
    }
    return false;
}

void clock_router::require_clock_participants(
    const std::unordered_set<simulator_index>* participants) const
{
    if (participants == nullptr) {
        return;
    }
    for (const auto& entry : simulators_) {
        if (entry.second.controller != nullptr &&
            participants->count(entry.first) == 0) {
            throw error(make_error_code(errc::unsupported_feature),
                entry.second.sim->name() +
                    ": a Clock event cannot be processed while a Clock-capable "
                    "simulator is ahead of the co-simulation time");
        }
    }
}

void clock_router::check_input_event_supported(time_point /*unused*/) const
{
    if (!has_clock_support()) {
        throw error(make_error_code(errc::unsupported_feature),
            "A Clock controller without Clock metadata cannot route an input event");
    }
}

bool clock_router::process_input_event(
    time_point currentTime,
    const std::unordered_set<simulator_index>* participants)
{
    check_recovery();
    check_input_event_supported(currentTime);
    return process_event(currentTime, true, participants);
}

bool clock_router::clock_feedback_path_exists(const data_connection& data) const
{
    std::vector<simulator_index> pending{data.target.simulator};
    std::unordered_set<simulator_index> visited;
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        if (current == data.source.simulator) {
            return true;
        }
        if (!visited.insert(current).second) {
            continue;
        }
        for (const auto& connection : clock_connections_) {
            if (connection.source.simulator == current) {
                pending.push_back(connection.target.simulator);
            }
        }
    }
    return false;
}

bool clock_router::due_clock_model_event(time_point currentTime) const
{
    return boost::algorithm::any_of(
        simulators_, [currentTime](const auto& entry) {
            const auto next = entry.second.sim->get_event_handling_status().next_event_time;
            return entry.second.controller &&
                (entry.second.controller->has_pending_clock_intervals() ||
                    (next && *next <= currentTime));
        });
}

bool clock_router::process_legacy_events(
    time_point currentTime,
    const std::unordered_set<simulator_index>& eventSources,
    const std::unordered_set<simulator_index>* participants)
{
    for (const auto& entry : simulators_) {
        if (entry.second.controller != nullptr) {
            continue;
        }
        if ((participants != nullptr) && participants->count(entry.first) == 0) {
            continue;
        }
        if (entry.second.sim->get_event_handling_status().termination_requested) {
            return true;
        }
        if (entry.second.sim->get_event_controller() != nullptr) {
            continue;
        }
        bool requested = eventSources.count(entry.first) != 0;
        std::size_t iterations = 0;
        while (true) {
            const auto status = entry.second.sim->get_event_handling_status();
            if (status.termination_requested) {
                return true;
            }
            if (!requested &&
                (!status.next_event_time || *status.next_event_time > currentTime)) {
                break;
            }
            if (++iterations > eventBudget_->limit()) {
                throw error(
                    make_error_code(errc::simulation_error),
                    "A simulator repeatedly scheduled an event at the current time");
            }
            entry.second.sim->handle_events();
            requested = false;
        }
    }
    return false;
}

const variable_description& clock_router::describe(variable_id identifier) const
{
    const auto simulatorIt = simulators_.find(identifier.simulator);
    if (simulatorIt == simulators_.end()) {
        throw error(
            make_error_code(errc::invalid_system_structure),
            "Clock connection references an unknown simulator");
    }
    const auto& variables = simulatorIt->second.description->variables;
    const auto found = boost::range::find_if(
        variables,
        [identifier](const auto& variable) {
            return variable.type == identifier.type &&
                variable.reference == identifier.reference;
        });
    if (found == variables.end()) {
        throw error(
            make_error_code(errc::invalid_system_structure),
            "Clock connection references an unknown model variable");
    }
    return *found;
}

clock_controller& clock_router::require_controller(simulator_index index) const
{
    const auto found = simulators_.find(index);
    if (found == simulators_.end() || found->second.controller == nullptr) {
        throw error(
            make_error_code(errc::unsupported_feature),
            "The simulator does not advertise the FMI 3 Clock capability");
    }
    return *found->second.controller;
}

void clock_router::activate_input_clock(variable_id clock)
{
    if (beforeInputActivation_) {
        beforeInputActivation_();
    }
    for (const auto& connection : clocked_data_connections_) {
        if (connection.target.simulator != clock.simulator) {
            continue;
        }
        const auto& target = describe(connection.target);
        if (boost::range::find(target.clocks, clock.reference) ==
            target.clocks.end()) {
            continue;
        }
        if (describe(connection.source).clocks.empty()) {
            if ((activeParticipants_ != nullptr) &&
                activeParticipants_->count(connection.source.simulator) == 0) {
                BOOST_LOG_SEV(log::logger(), log::warning)
                    << simulators_.at(connection.source.simulator).sim->name()
                    << " is ahead of the Clock event at "
                    << to_double_time_point(activeEventTime_)
                    << " s; the last staged value will be used for "
                    << simulators_.at(connection.target.simulator).sim->name() << ".";
                continue;
            }
            const auto value = simulators_.at(connection.source.simulator).sim->get_variable_value(connection.source.type, connection.source.reference);
            simulators_.at(connection.target.simulator).sim->stage_clocked_input(connection.target.type, connection.target.reference, value);
        }
    }
    require_controller(clock.simulator).set_input_clock(clock.reference, true);
}

bool clock_router::clock_routed(const data_connection& data) const
{
    const auto& source = describe(data.source);
    const auto& target = describe(data.target);
    if (source.clocks.empty() || target.clocks.empty()) {
        return false;
    }
    return boost::algorithm::any_of(
        clock_connections_,
        [&data, &target](const auto& clock) {
            return clock.source.simulator == data.source.simulator &&
                clock.target.simulator == data.target.simulator &&
                std::find(target.clocks.begin(), target.clocks.end(),
                    clock.target.reference) != target.clocks.end();
        });
}

bool clock_router::depends_on_due_clock(
    variable_id clock,
    const std::unordered_set<variable_id>& pendingClocks) const
{
    for (const auto& connection : clocked_data_connections_) {
        if (connection.target.simulator != clock.simulator ||
            connection.source.simulator == clock.simulator || clock_routed(connection)) {
            continue;
        }
        const auto& source = describe(connection.source);
        const auto& target = describe(connection.target);
        if (boost::range::find(target.clocks, clock.reference) ==
            target.clocks.end()) {
            continue;
        }
        for (const auto reference : source.clocks) {
            const variable_id sourceClock{
                connection.source.simulator,
                variable_type::clock,
                reference,
            };
            if (pendingClocks.count(sourceClock) != 0) {
                return true;
            }
            for (const auto dependency : describe(sourceClock).clocks) {
                if (pendingClocks.count(
                        {connection.source.simulator, variable_type::clock, dependency}) != 0) {
                    return true;
                }
            }
        }
    }
    return false;
}

bool clock_router::probe_source_clocks(
    variable_id source,
    std::unordered_set<variable_id>& firedOutputs,
    const std::unordered_set<variable_id>& activatedInputs)
{
    bool active = false;
    for (const auto reference : describe(source).clocks) {
        const variable_id clock{source.simulator, variable_type::clock, reference};
        if (firedOutputs.count(clock) != 0 || activatedInputs.count(clock) != 0) {
            active = true;
            continue;
        }
        const auto& description = describe(clock);
        if (description.causality != variable_causality::output ||
            !has_active_clock(description, source.simulator, firedOutputs, activatedInputs)) {
            continue;
        }
        if (require_controller(source.simulator).get_output_clock(reference)) {
            firedOutputs.insert(clock);
            active = true;
        }
    }
    return active;
}

void clock_router::deliver_data(variable_id target, const variable_value& value)
{
    auto* sim = simulators_.at(target.simulator).sim;
    const auto& variable = describe(target);
    if (!variable.clocks.empty()) {
        sim->stage_clocked_input(target.type, target.reference, value);
        return;
    }
    if (sim->get_event_controller() != nullptr &&
        variable.causality == variable_causality::input &&
        variable.variability == variable_variability::continuous) {
        sim->signal_input_discontinuity(target.type, target.reference);
    }
    sim->set_variable_value(target.type, target.reference, value);
}

void clock_router::deliver_unrouted_data(
    std::unordered_set<variable_id>& firedOutputs,
    const std::unordered_set<variable_id>& activatedInputs,
    std::unordered_set<variable_id>& transferredData)
{
    for (const auto& connection : clocked_data_connections_) {
        if (transferredData.count(connection.target) != 0 ||
            describe(connection.source).clocks.empty() ||
            clock_routed(connection)) {
            continue;
        }
        if (!probe_source_clocks(connection.source, firedOutputs, activatedInputs)) {
            continue;
        }
        const auto value = require_controller(connection.source.simulator)
                               .get_clocked_variable(
                                   connection.source.type, connection.source.reference);
        deliver_data(connection.target, value);
        transferredData.insert(connection.target);
    }
}

void clock_router::retract_deactivated_outputs(
    simulator_index origin,
    std::unordered_set<variable_id>& firedOutputs,
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): These mutable sets track distinct event state.
    std::unordered_set<variable_id>& activatedInputs,
    std::unordered_set<variable_id>& transferredData,
    std::unordered_set<std::size_t>& propagatedConnections,
    std::vector<observed_clocked_data>& observedData)
{
    std::vector<simulator_index> worklist{origin};
    const auto maxRetractions = eventBudget_->limit();
    while (!worklist.empty()) {
        const auto index = worklist.back();
        worklist.pop_back();
        std::vector<variable_id> retracted;
        for (const auto& output : firedOutputs) {
            if (output.simulator == index &&
                !require_controller(index).probe_output_clock(output.reference)) {
                retracted.push_back(output);
            }
        }
        for (const auto& output : retracted) {
            if (++retractions_ > maxRetractions) {
                throw error(
                    make_error_code(errc::simulation_error),
                    "Clock deactivation did not converge within " +
                        std::to_string(maxRetractions) + " retractions");
            }
            firedOutputs.erase(output);
            for (std::size_t i = 0; i < clock_connections_.size(); ++i) {
                const auto& connection = clock_connections_.at(i);
                if (!(connection.source == output) || propagatedConnections.erase(i) == 0 ||
                    activatedInputs.count(connection.target) == 0) {
                    continue;
                }
                const auto& target = describe(connection.target);
                if (!target.clock || !target.clock->can_be_deactivated) {
                    throw error(
                        make_error_code(errc::unsupported_feature),
                        "The output Clock '" + describe(output).name +
                            "' was deactivated, but the Clock '" + target.name +
                            "' it activated cannot be deactivated");
                }
                require_controller(connection.target.simulator)
                    .set_input_clock(connection.target.reference, false);
                activatedInputs.erase(connection.target);
                clear_transfers_for_clock(connection.target, transferredData);
                worklist.push_back(connection.target.simulator);
            }
        }
    }
    observedData.erase(
        boost::range::remove_if(
            observedData,
            [&](const auto& observed) {
                return !has_active_clock(
                    describe(observed.source), observed.source.simulator,
                    firedOutputs, activatedInputs);
            }),
        observedData.end());
}

void clock_router::clear_transfers_for_clock(
    variable_id target, std::unordered_set<variable_id>& transferredData) const
{
    for (const auto& data : clocked_data_connections_) {
        const auto& dataTarget = describe(data.target);
        if (data.target.simulator == target.simulator &&
            boost::range::find(dataTarget.clocks, target.reference) != dataTarget.clocks.end()) {
            transferredData.erase(data.target);
        }
    }
}

void clock_router::verify_clocked_data_consistency(
    simulator_index target,
    const std::unordered_set<variable_id>& firedOutputs,
    const std::unordered_set<variable_id>& activatedInputs,
    const std::vector<observed_clocked_data>& observedData) const
{
    for (const auto& observed : observedData) {
        if (observed.source.simulator != target) {
            continue;
        }
        if (!has_active_clock(
                describe(observed.source), observed.source.simulator,
                firedOutputs, activatedInputs) ||
            !detail::event_values_close(
                observed.rawValue,
                require_controller(observed.source.simulator)
                    .probe_clocked_variable(
                        observed.source.type, observed.source.reference),
                eventValueTolerance_)) {
            throw error(
                make_error_code(errc::unsupported_feature),
                "Clock-associated data changed after its delivery during the same propagation sweep");
        }
    }
}

const std::vector<std::size_t>& clock_router::linear_clock_order()
{
    if (!clock_order_dirty_) {
        return clock_order_;
    }
    clock_order_ = build_linear_clock_order();
    clock_order_dirty_ = false;
    return clock_order_;
}

std::vector<std::size_t> clock_router::build_linear_clock_order() const
{
    if (clock_connections_.size() < 2) {
        return {};
    }
    std::unordered_map<simulator_index, std::size_t> incoming;
    std::unordered_map<simulator_index, std::size_t> outgoing;
    for (std::size_t i = 0; i < clock_connections_.size(); ++i) {
        const auto& connection = clock_connections_.at(i);
        if (!outgoing.emplace(connection.source.simulator, i).second ||
            !incoming.emplace(connection.target.simulator, i).second) {
            return {};
        }
        const auto& variables = simulators_.at(connection.source.simulator).description->variables;
        const auto [inputClock, multipleInputs] = sole_input_clock(variables);
        if (multipleInputs) {
            return {};
        }
        const auto& source = describe(connection.source);
        if (inputClock == nullptr || source.clocks.size() != 1 ||
            source.clocks.front() != inputClock->reference) {
            return {};
        }
    }

    std::optional<simulator_index> root;
    for (const auto& edge : outgoing) {
        if (incoming.count(edge.first) == 0) {
            if (root) {
                return {};
            }
            root = edge.first;
        }
    }
    if (!root) {
        return {};
    }
    std::vector<std::size_t> order;
    auto current = *root;
    while (true) {
        const auto edge = outgoing.find(current);
        if (edge == outgoing.end()) {
            break;
        }
        order.push_back(edge->second);
        if (order.size() > clock_connections_.size()) {
            return {};
        }
        current = clock_connections_.at(edge->second).target.simulator;
    }
    if (order.size() != clock_connections_.size()) {
        return {};
    }
    return order;
}

bool clock_router::discover_outputs(
    std::unordered_set<variable_id>& firedOutputs,
    const std::unordered_set<variable_id>& activatedInputs)
{
    bool discovered = false;
    for (const auto& connection : clock_connections_) {
        if (firedOutputs.count(connection.source) != 0 ||
            !has_active_clock(
                describe(connection.source), connection.source.simulator,
                firedOutputs, activatedInputs)) {
            continue;
        }
        if (require_controller(connection.source.simulator)
                .get_output_clock(connection.source.reference)) {
            firedOutputs.insert(connection.source);
            discovered = true;
        }
    }
    for (const auto& connection : clocked_data_connections_) {
        if (describe(connection.source).clocks.empty() || clock_routed(connection)) {
            continue;
        }
        const auto firedBefore = firedOutputs.size();
        probe_source_clocks(connection.source, firedOutputs, activatedInputs);
        discovered = discovered || firedOutputs.size() != firedBefore;
    }
    return discovered;
}

std::vector<std::pair<variable_id, variable_value>> clock_router::capture_clocked_data(
    const clock_connection& clockConnection,
    const std::unordered_set<variable_id>& firedOutputs,
    const std::unordered_set<variable_id>& activatedInputs,
    std::unordered_set<variable_id>& transferredData,
    std::vector<observed_clocked_data>& observedData)
{
    std::vector<std::pair<variable_id, variable_value>> values;
    for (const auto& connection : clocked_data_connections_) {
        if (transferredData.count(connection.target) != 0 ||
            connection.source.simulator != clockConnection.source.simulator ||
            connection.target.simulator != clockConnection.target.simulator) {
            continue;
        }
        const auto& source = describe(connection.source);
        const auto& target = describe(connection.target);
        if (source.clocks.empty()) {
            continue;
        }
        if (boost::range::find(target.clocks,
                clockConnection.target.reference) == target.clocks.end()) {
            continue;
        }
        if (!has_active_clock(
                source,
                connection.source.simulator,
                firedOutputs,
                activatedInputs)) {
            throw error(
                make_error_code(errc::unsupported_feature),
                "Clock-associated data could not be transferred from '" +
                    source.name + "' to '" + target.name +
                    "': no associated source Clock is active");
        }
        auto& sourceController =
            require_controller(connection.source.simulator);
        observedData.push_back({
            connection.source,
            sourceController.probe_clocked_variable(
                connection.source.type, connection.source.reference),
        });
        values.emplace_back(connection.target,
            sourceController.get_clocked_variable(
                connection.source.type, connection.source.reference));
        transferredData.insert(connection.target);
    }
    return values;
}

bool clock_router::has_active_clock(
    const variable_description& variable,
    simulator_index simulator,
    const std::unordered_set<variable_id>& firedOutputs,
    const std::unordered_set<variable_id>& activatedInputs)
{
    if (variable.clocks.empty()) {
        return true;
    }
    return boost::algorithm::any_of(
        variable.clocks,
        [simulator, &firedOutputs, &activatedInputs](const auto reference) {
            const variable_id clock{
                simulator,
                variable_type::clock,
                reference,
            };
            return firedOutputs.count(clock) != 0 ||
                activatedInputs.count(clock) != 0;
        });
}

bool clock_router::process_event(
    time_point currentTime,
    bool roundAlreadyAcquired,
    const std::unordered_set<simulator_index>* participants)
{
    // Event handling is iterated to a fixed point because a Clock update
    // may activate another Clock in the same superdense timestamp.
    if (!has_clock_support()) {
        return false;
    }
    check_recovery();
    if (eventBudget_ == nullptr) {
        throw error(make_error_code(errc::simulation_error),
            "The Clock router has no event iteration budget");
    }
    validate_pending_intervals(currentTime);
    struct participant_guard
    {
        explicit participant_guard(const std::unordered_set<simulator_index>*& active)
            : slot(active)
            , previous(active)
        { }
        participant_guard(const participant_guard&) = delete;
        participant_guard& operator=(const participant_guard&) = delete;
        participant_guard(participant_guard&&) = delete;
        participant_guard& operator=(participant_guard&&) = delete;
        const std::unordered_set<simulator_index>*& slot;
        const std::unordered_set<simulator_index>* previous;
        ~participant_guard() { slot = previous; }
    };
    const participant_guard guard{activeParticipants_};
    activeParticipants_ = participants;
    activeEventTime_ = currentTime;
    try {
        return process_event_unchecked(currentTime, roundAlreadyAcquired);
    } catch (...) {
        recovery_required_ = true;
        throw;
    }
}

bool clock_router::process_event_unchecked(time_point currentTime, bool roundAlreadyAcquired)
{
    bool routingEnabled = enter_clock_event(currentTime, roundAlreadyAcquired);
    clock_event_work work;
    if (routingEnabled) {
        activate_due_clock_waves(currentTime, work);
    }
    const auto maxLocalIterations = eventBudget_->limit();
    while (true) {
        if (routingEnabled) {
            route_clock_event_outputs(work);
        }
        const auto [updateRequested, terminationRequested] =
            update_clock_event_participants(currentTime, work, maxLocalIterations);
        if (terminationRequested) {
            return true;
        }
        work.firedOutputs.clear();
        work.activatedInputs.clear();
        work.transferredData.clear();
        work.propagatedConnections.clear();
        work.observedData.clear();
        bool newOutputs = false;
        if (routingEnabled) {
            newOutputs = discover_outputs(work.firedOutputs, work.activatedInputs);
        }
        const bool changingDue = boost::algorithm::any_of(
            changing_clocks_,
            [currentTime](const auto& clock) {
                return clock.next && *clock.next <= currentTime;
            });
        if (routingEnabled && (newOutputs || changingDue) &&
            !eventBudget_->acquire(currentTime)) {
            if (changingDue) {
                throw error(make_error_code(errc::simulation_error),
                    "Event iteration limit exhausted with a time-based FMI 3 Clock tick due");
            }
            eventBudget_->warn(currentTime);
            routingEnabled = false;
            newOutputs = false;
        }
        const bool activatedChanging = activate_pending_changing_clocks(currentTime, routingEnabled, work);
        if (!updateRequested && !newOutputs && !activatedChanging) {
            break;
        }
    }
    finish_clock_event(currentTime, work.modelNextEvent);
    return terminated_;
}

void clock_router::finish_clock_event(
    time_point currentTime, const std::optional<time_point>& modelNextEvent)
{
    advance_periodic_clocks(currentTime);
    next_event_time_ = modelNextEvent;
    for (const auto& clock : periodic_clocks_) {
        if (!next_event_time_ || clock.next < *next_event_time_) {
            next_event_time_ = clock.next;
        }
    }
    for (const auto& clock : changing_clocks_) {
        if (clock.next && (!next_event_time_ || *clock.next < *next_event_time_)) {
            next_event_time_ = clock.next;
        }
    }

    if (!terminated_) {
        for (const auto& entry : simulators_) {
            if (entry.second.controller != nullptr &&
                entry.second.controller->is_in_event_mode()) {
                entry.second.sim->refresh_event_values();
                entry.second.controller->enter_step_mode();
            }
        }
    }
}

void clock_router::route_clock_event_outputs(clock_event_work& work)
{
    bool changed = true;
    while (changed) {
        const auto firedBefore = work.firedOutputs.size();
        deliver_unrouted_data(work.firedOutputs, work.activatedInputs, work.transferredData);
        changed = work.firedOutputs.size() != firedBefore;
        changed = propagate_outputs(
                      work.firedOutputs, work.activatedInputs, work.transferredData,
                      work.propagatedConnections, work.observedData,
                      [](std::size_t) { }) ||
            changed;
    }
}

clock_router::clock_event_update clock_router::update_clock_event_participants(
    time_point currentTime, clock_event_work& work, std::size_t maxLocalIterations)
{
    bool updateRequested = false;
    work.modelNextEvent.reset();
    for (const auto& entry : simulators_) {
        if (entry.second.controller == nullptr) {
            continue;
        }
        const auto result = entry.second.controller->update_clock_state();
        if (result.terminate_simulation) {
            terminated_ = true;
            return {false, true};
        }
        bool localUpdate = result.discrete_states_need_update;
        if (result.next_event_time) {
            if (*result.next_event_time <= currentTime) {
                localUpdate = true;
            } else if (!work.modelNextEvent ||
                *result.next_event_time < *work.modelNextEvent) {
                work.modelNextEvent = result.next_event_time;
            }
        }
        if (localUpdate) {
            if (++work.localIterations[entry.first] > maxLocalIterations) {
                throw error(
                    make_error_code(errc::simulation_error),
                    "An FMI 3 Clock event did not converge within the "
                    "FMU-local iteration limit");
            }
            updateRequested = true;
        } else {
            work.localIterations.erase(entry.first);
            refresh_changing_clock_intervals(
                entry.first, *entry.second.controller, currentTime, work.pendingChangingReads);
        }
    }
    return {updateRequested, false};
}

bool clock_router::activate_pending_changing_clocks(
    time_point currentTime, bool routingEnabled, clock_event_work& work)
{
    bool activatedChanging = false;
    for (auto& clock : changing_clocks_) {
        if (!clock.next || *clock.next > currentTime) {
            continue;
        }
        if (!routingEnabled) {
            throw error(make_error_code(errc::simulation_error),
                "Event iteration limit exhausted with a time-based FMI 3 Clock tick due");
        }
        activate_scheduled_changing_clock(clock, currentTime, work);
        activatedChanging = true;
    }
    return activatedChanging;
}

bool clock_router::enter_clock_event(time_point currentTime, bool roundAlreadyAcquired)
{
    const bool routingEnabled = roundAlreadyAcquired || eventBudget_->acquire(currentTime);
    retractions_ = 0;
    if (!routingEnabled) {
        const auto periodicDue = boost::algorithm::any_of(periodic_clocks_,
            [currentTime](const auto& clock) { return clock.next <= currentTime; });
        const auto aperiodicDue = boost::algorithm::any_of(changing_clocks_,
            [currentTime](const auto& clock) { return clock.next && *clock.next <= currentTime; });
        if (periodicDue || aperiodicDue) {
            throw error(make_error_code(errc::simulation_error),
                "Event iteration limit exhausted with a time-based FMI 3 Clock tick due");
        }
        eventBudget_->warn(currentTime);
    }
    for (const auto& entry : simulators_) {
        if (entry.second.controller != nullptr &&
            !entry.second.controller->is_in_event_mode()) {
            entry.second.controller->enter_event_mode();
        }
    }
    if (routingEnabled) {
        for (const auto& entry : simulators_) {
            if (entry.second.controller != nullptr) {
                entry.second.controller->apply_pending_clock_intervals();
            }
        }
    }
    return routingEnabled;
}

std::unordered_set<variable_id> clock_router::due_clock_ids(time_point currentTime) const
{
    std::unordered_set<variable_id> unactivatedClocks;
    for (const auto& clock : periodic_clocks_) {
        if (clock.next <= currentTime) {
            unactivatedClocks.insert(clock.id);
        }
    }
    for (const auto& clock : changing_clocks_) {
        if (clock.next && *clock.next <= currentTime) {
            unactivatedClocks.insert(clock.id);
        }
    }
    return unactivatedClocks;
}

void clock_router::activate_due_clock_waves(time_point currentTime, clock_event_work& work)
{
    auto unactivatedClocks = due_clock_ids(currentTime);
    // A Clock whose input data is sampled from another Clock that is due
    // at the same time is activated after that sample is taken. Mutually
    // dependent Clocks are activated together and see the previous sample.
    while (!unactivatedClocks.empty()) {
        std::unordered_set<variable_id> wave;
        for (const auto& identifier : unactivatedClocks) {
            if (!depends_on_due_clock(identifier, unactivatedClocks)) {
                wave.insert(identifier);
            }
        }
        if (wave.empty()) {
            wave = unactivatedClocks;
        }
        for (const auto& clock : periodic_clocks_) {
            if (wave.count(clock.id) != 0) {
                activate_input_clock(clock.id);
                work.activatedInputs.insert(clock.id);
            }
        }
        for (auto& clock : changing_clocks_) {
            if (wave.count(clock.id) != 0) {
                activate_scheduled_changing_clock(clock, currentTime, work);
            }
        }
        for (const auto& identifier : wave) {
            unactivatedClocks.erase(identifier);
        }
        if (!unactivatedClocks.empty()) {
            deliver_unrouted_data(work.firedOutputs, work.activatedInputs, work.transferredData);
        }
    }
}

void clock_router::activate_scheduled_changing_clock(
    changing_clock& clock, time_point currentTime, clock_event_work& work)
{
    activate_changing_clock(clock, currentTime);
    activate_input_clock(clock.id);
    work.activatedInputs.insert(clock.id);
    work.pendingChangingReads.insert(clock.id);
    if (clock.countdown) {
        clock.known = false;
    }
    clock.next.reset();
}

void clock_router::refresh_changing_clock_intervals(simulator_index index, clock_controller& controller,
    time_point currentTime, std::unordered_set<variable_id>& pendingReads)
{
    for (auto& clock : changing_clocks_) {
        if (clock.id.simulator != index) {
            continue;
        }
        const bool activated = pendingReads.count(clock.id) != 0;
        if (clock.tunable) {
            refresh_tunable_clock(clock, controller, currentTime, activated);
        } else {
            if (!clock.countdown && !activated) {
                continue;
            }
            refresh_aperiodic_clock(clock, controller, currentTime, activated);
        }
        pendingReads.erase(clock.id);
    }
}

void clock_router::refresh_tunable_clock(changing_clock& clock, clock_controller& controller,
    time_point currentTime, bool activated)
{
    clock_interval_update update{clock_interval_qualifier::unchanged, 0.0, {}, {}};
    if (clock.fmu_controlled) {
        update = controller.get_input_clock_interval(clock.id.reference);
    } else {
        const auto schedule = controller.resolved_input_clock_schedule(clock.id.reference);
        if (!schedule || !schedule->interval_decimal) {
            throw error(make_error_code(errc::model_error),
                "A tunable FMI 3 Clock has no resolved interval");
        }
        update = {
            clock_interval_qualifier::changed,
            *schedule->interval_decimal,
            schedule->interval_counter,
            schedule->resolution,
        };
    }
    if (update.qualifier == clock_interval_qualifier::changed) {
        validate_tunable_interval(update);
        clock.interval = update;
    } else if (update.qualifier != clock_interval_qualifier::unchanged) {
        throw error(make_error_code(errc::model_error),
            "A tunable FMI 3 Clock returned an invalid interval qualifier");
    }
    if (clock.ticked) {
        clock.next = changing_time(clock);
        if (*clock.next < currentTime || (activated && *clock.next == currentTime)) {
            throw error(make_error_code(errc::unsupported_feature),
                "A tunable FMI 3 Clock interval schedules an unrepresentable or past tick");
        }
    }
}

void clock_router::refresh_aperiodic_clock(changing_clock& clock, clock_controller& controller,
    time_point currentTime, bool activated)
{
    const auto update = controller.get_input_clock_interval(clock.id.reference);
    if (update.qualifier == clock_interval_qualifier::changed) {
        clock.interval = update;
        clock.known = true;
        if (!activated) {
            clock.origin = currentTime;
            clock.origin_numerator = currentTime.time_since_epoch().count();
            clock.origin_denominator = 1;
        }
        clock.next = changing_time(clock);
    } else if (update.qualifier == clock_interval_qualifier::unchanged &&
        clock.known) {
        if (!clock.countdown) {
            clock.next = changing_time(clock);
        }
    } else if (update.qualifier == clock_interval_qualifier::not_yet_known &&
        clock.countdown && !clock.known) {
        clock.next.reset();
    } else {
        throw error(make_error_code(errc::model_error),
            "An FMI 3 aperiodic Clock returned an invalid interval qualifier");
    }
}

} // namespace cosim
