/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/algorithm/ecco_algorithm.hpp"

#include "clock_router.hpp"
#include "event_router.hpp"
#include "step_batch.hpp"

#include "cosim/algorithm/algorithm.hpp"
#include "cosim/algorithm/connection_validation.hpp"
#include "cosim/algorithm/simulator.hpp"
#include "cosim/error.hpp"
#include "cosim/exception.hpp"
#include "cosim/execution.hpp"
#include "cosim/function/function.hpp"
#include "cosim/log/logger.hpp"
#include "cosim/model_description.hpp"
#include "cosim/serialization.hpp"
#include "cosim/time.hpp"
#include "cosim/utility/thread_pool.hpp"

#include <boost/algorithm/cxx11/any_of.hpp>
#include <boost/algorithm/cxx11/none_of.hpp>
#include <boost/log/sources/severity_feature.hpp>
#include <boost/property_tree/exceptions.hpp>
#include <boost/range/algorithm/find_if.hpp>
#include <boost/range/algorithm/max_element.hpp>
#include <boost/range/algorithm/remove_if.hpp>
#include <boost/range/algorithm/stable_sort.hpp>
#include <gsl/util>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>


namespace cosim
{

class ecco_algorithm::impl
{
public:
    explicit impl(ecco_algorithm_params params, std::optional<unsigned int> workerThreadCount)
        : params_(params)
        , stepSize_(params.step_size)
        , pool_(std::min(workerThreadCount.value_or(max_threads_), max_threads_))
    {
        COSIM_INPUT_CHECK(params_.min_step_size.count() > 0);
        COSIM_INPUT_CHECK(params_.step_size >= params_.min_step_size);
        COSIM_INPUT_CHECK(params_.step_size <= params_.max_step_size);
        eventRouter_.set_max_event_iterations(params_.max_event_iterations);
        set_event_value_tolerance(params_.event_value_tolerance);
        clockRouter_.set_event_budget(eventRouter_.budget());
    }

    ~impl() noexcept = default;

    impl(const impl&) = delete;
    impl& operator=(const impl&) = delete;

    impl(impl&&) = delete;
    impl& operator=(impl&&) = delete;

    void add_simulator(simulator_index index, simulator* sim, [[maybe_unused]] duration stepSizeHint)
    {
        assert(simulators_.count(index) == 0);
        const auto description = std::make_shared<const model_description>(sim->model_description());
        simulators_[index].sim = sim;
        simulators_[index].capabilities = description->capabilities;
        clockRouter_.add_simulator(index, sim, description);
        eventRouter_.add_simulator(index, sim, description);
    }

    void remove_simulator(simulator_index index)
    {
        clockRouter_.remove_simulator(index);
        eventRouter_.remove_simulator(index);
        simulators_.erase(index);
        disconnect_simulator_variables(index);
    }

    void add_function(function_index index, function* fun)
    {
        assert(functions_.count(index) == 0);
        functions_[index].fun = fun;
        eventRouter_.add_function(index, fun);
    }

    void connect_variables(variable_id output, variable_id input)
    {
        detail::validate_connection_types(output.type, input.type);
        if (output.type == variable_type::clock ||
            input.type == variable_type::clock) {
            clockRouter_.connect_clock(output, input);
            return;
        }
        auto& sourceSimInfo = simulators_.at(output.simulator);
        const auto& targetSimInfo = simulators_.at(input.simulator);
        sourceSimInfo.sim->expose_for_getting(output.type, output.reference);
        targetSimInfo.sim->expose_for_setting(input.type, input.reference);
        const bool clocked = clockRouter_.connect_data(output, input);
        if (!clocked) {
            eventRouter_.connect_data(output, input);
        }
        sourceSimInfo.outgoingSimConnections.push_back({output, input, clocked});
    }

    void connect_variables(variable_id output, function_io_id input)
    {
        detail::validate_connection_types(output.type, input.type);
        auto& simInfo = simulators_.at(output.simulator);
        simInfo.sim->expose_for_getting(output.type, output.reference);
        eventRouter_.connect_function(output, input);
        simInfo.outgoingFunConnections.push_back({output, input});
    }

    void connect_variables(function_io_id output, variable_id input)
    {
        detail::validate_connection_types(output.type, input.type);
        auto& funInfo = functions_.at(output.function);
        const auto& simInfo = simulators_.at(input.simulator);
        simInfo.sim->expose_for_setting(input.type, input.reference);
        eventRouter_.connect_function(output, input);
        funInfo.outgoingSimConnections.push_back({output, input});
    }

    void connect_variables(function_io_id output, function_io_id input)
    {
        detail::validate_connection_types(output.type, input.type);
        eventRouter_.connect_function(output, input);
        functions_.at(output.function)
            .outgoingFunConnections.push_back({output, input});
    }

    void disconnect_variable(variable_id input)
    {
        eventRouter_.disconnect(input);
        clockRouter_.disconnect(input);
        for (auto& simulator : simulators_) {
            auto& conns = simulator.second.outgoingSimConnections;
            const auto found = boost::range::find_if(
                conns,
                [input](const auto& connection) { return connection.target == input; });
            if (found != conns.end()) {
                conns.erase(found);
                // There can be only one connection to an input variable,
                // so we return as early as possible.
                return;
            }
        }
    }

    void disconnect_variable(function_io_id input)
    {
        eventRouter_.disconnect(input);
        for (auto& simulator : simulators_) {
            auto& conns = simulator.second.outgoingFunConnections;
            const auto found = boost::range::find_if(
                conns,
                [input](const auto& connection) { return connection.target == input; });
            if (found != conns.end()) {
                conns.erase(found);
                // There can be only one connection to an input variable,
                // so we return as early as possible.
                return;
            }
        }
        for (auto& function : functions_) {
            auto& conns = function.second.outgoingFunConnections;
            const auto found = boost::range::find_if(
                conns,
                [input](const auto& connection) { return connection.target == input; });
            if (found != conns.end()) {
                conns.erase(found);
                return;
            }
        }
    }

    void setup(time_point startTime, std::optional<time_point> stopTime)
    {
        startTime_ = startTime;
        stopTime_ = stopTime;
    }

    void set_max_event_iterations(std::size_t limit)
    {
        if (initialized_) {
            throw error(make_error_code(errc::simulation_error),
                "The event iteration limit must be configured before initialization");
        }
        eventRouter_.set_max_event_iterations(limit);
    }

    duration initial_step_size() const noexcept { return params_.step_size; }
    std::size_t worker_thread_count() const noexcept { return pool_.numWorkerThreads(); }
    std::size_t max_event_iterations() const noexcept { return eventRouter_.budget().limit(); }

    void set_event_value_tolerance(cosim::event_value_tolerance tolerance)
    {
        if (initialized_) {
            throw error(make_error_code(errc::simulation_error),
                "The event value tolerance must be configured before initialization");
        }
        eventRouter_.set_event_value_tolerance(tolerance);
        clockRouter_.set_event_value_tolerance(tolerance);
    }

    cosim::event_value_tolerance event_value_tolerance() const noexcept
    {
        return eventRouter_.value_tolerance();
    }

    void initialize()
    {
        if (initializationFailed_) {
            throw error(make_error_code(errc::simulation_error),
                "The algorithm cannot continue after failed initialization; recreate the execution");
        }
        try {
            initialize_impl();
        } catch (...) {
            initializationFailed_ = true;
            throw;
        }
    }

    void initialize_impl()
    {
        initialized_ = true;
        eventRouter_.warn_about_mixed_capability_connections(clockRouter_);
        for (const auto& entry : simulators_) {
            auto* simulator = entry.second.sim;
            pool_.submit([this, simulator] {
                simulator->set_max_event_iterations(eventRouter_.budget().limit());
                simulator->setup(startTime_, stopTime_, std::nullopt);
            });
        }
        pool_.wait_for_tasks_to_finish();

        // Run N iterations of the simulators' and functions' step/calculation
        // procedures, where N is the number of simulators in the system,
        // to propagate initial values.
        for (std::size_t i = 0; i < simulators_.size() + functions_.size(); ++i) {
            for (auto& entry : simulators_) {
                pool_.submit([&] {
                    entry.second.sim->do_iteration();
                });
            }
            pool_.wait_for_tasks_to_finish();

            for (const auto& entry : simulators_) {
                transfer_variables(entry.second.outgoingSimConnections, true);
                transfer_variables(entry.second.outgoingFunConnections);
            }
            calculate_and_transfer_functions();
        }

        for (auto& entry : simulators_) {
            pool_.submit([&] {
                entry.second.sim->start_simulation();
            });
        }
        pool_.wait_for_tasks_to_finish();
        if (boost::algorithm::any_of(simulators_, [](const auto& entry) {
                return entry.second.sim->get_event_handling_status().termination_requested;
            })) {
            throw error(make_error_code(errc::simulation_error),
                "A simulator requested termination during initialization");
        }
        eventRouter_.initialize(startTime_, stepSize_, clockRouter_);
        for (const auto& entry : simulators_) {
            transfer_variables(entry.second.outgoingSimConnections, false);
        }
    }

    algorithm::macro_step_result do_step_with_info(time_point currentT)
    {
        check_step_usable(clockRouter_, initializationFailed_, recoveryRequired_);
        clockRouter_.validate_pending_intervals(currentT);
        try {
            return do_step_with_info_impl(currentT);
        } catch (...) {
            recoveryRequired_ = true;
            throw;
        }
    }

    duration prepare_step_participants(
        time_point currentT, std::unordered_set<simulator_index>& participants)
    {
        auto participantLimit = stepSize_;
        if (interruptedEnd_) {
            if (*interruptedEnd_ <= currentT) {
                interruptedEnd_.reset();
            } else {
                participantLimit = std::min(participantLimit, *interruptedEnd_ - currentT);
            }
        }
        for (auto& [index, info] : simulators_) {
            if (!info.reached) {
                info.reached = currentT;
            }
            if (*info.reached < currentT) {
                throw error(make_error_code(errc::simulation_error),
                    info.sim->name() + ": the simulator is behind the co-simulation time");
            }
            info.stepInfo = {};
            if (*info.reached == currentT) {
                participants.insert(index);
            } else {
                participantLimit = std::min(participantLimit, *info.reached - currentT);
            }
        }
        return participantLimit;
    }

    std::optional<algorithm::macro_step_result> early_termination_result(
        time_point currentT, duration requestedStepSize,
        const std::unordered_set<simulator_index>& earlySources) const
    {
        for (const auto index : earlySources) {
            const auto& info = simulators_.at(index);
            if (info.stepInfo.termination_requested) {
                std::unordered_set<simulator_index> stopped;
                for (const auto source : earlySources) {
                    if (*simulators_.at(source).reached == currentT + requestedStepSize) { // NOLINT(bugprone-unchecked-optional-access): Early sources record their endpoint before being returned.
                        stopped.insert(source);
                    }
                }
                return algorithm::macro_step_result{
                    requestedStepSize,
                    std::move(stopped),
                    requestedStepSize != stepSize_,
                    true,
                };
            }
        }
        return std::nullopt;
    }

    struct participant_step_results // NOLINT(bugprone-exception-escape): MSVC unordered-container moves may allocate.
    {
        time_point endTime;
        std::unordered_set<simulator_index> finished;
        std::unordered_set<simulator_index> eventSources;
        bool earlyReturn = false;
        bool terminationRequested = false;
    };

    void submit_participant_steps(
        step_batch& batch, time_point currentT, duration requestedStepSize,
        const std::unordered_set<simulator_index>& participants,
        const std::unordered_set<simulator_index>& earlySources)
    {
        for (auto& entry : simulators_) {
            if (participants.count(entry.first) == 0 || earlySources.count(entry.first) != 0) {
                continue;
            }
            auto& info = entry.second;
            if (requestedStepSize == duration::zero()) {
                info.stepInfo = {step_result::complete, currentT};
            } else {
                batch.submit(*info.sim, info.stepInfo, currentT, requestedStepSize);
            }
        }
        ++stepCounter_;
        batch.join();
    }

    participant_step_results collect_participant_steps(
        time_point currentT, duration& requestedStepSize,
        const std::unordered_set<simulator_index>& participants, // NOLINT(bugprone-easily-swappable-parameters): Participants and early sources have distinct roles.
        const std::unordered_set<simulator_index>& earlySources)
    {
        for (const auto index : participants) {
            auto& info = simulators_.at(index);
            if (earlySources.count(index) == 0) {
                info.reached = info.stepInfo.early_return
                    ? info.stepInfo.last_successful_time
                    : currentT + requestedStepSize;
                info.eventPending = info.stepInfo.event_handling_needed;
            }
        }
        auto endTime = currentT + requestedStepSize;
        for (const auto index : participants) {
            endTime = std::min(endTime, *simulators_.at(index).reached); // NOLINT(bugprone-unchecked-optional-access): Every participant has a reached time after stepping.
        }
        if (endTime < currentT + requestedStepSize) {
            for (const auto index : participants) {
                if (*simulators_.at(index).reached != endTime) { // NOLINT(bugprone-unchecked-optional-access): Every participant has a reached time after stepping.
                    throw error(make_error_code(errc::simulation_error),
                        "Simulators reached different step endpoints after an undeclared early return");
                }
            }
            requestedStepSize = endTime - currentT;
        }
        participant_step_results result{endTime, {}, {}};
        for (const auto& [idx, info] : simulators_) {
            if (*info.reached > endTime) { // NOLINT(bugprone-unchecked-optional-access): All simulator times were initialized before stepping.
                continue;
            }
            if (*info.reached < endTime) { // NOLINT(bugprone-unchecked-optional-access): All simulator times were initialized before stepping.
                throw error(make_error_code(errc::simulation_error),
                    info.sim->name() + ": reached an unexpected step endpoint");
            }
            result.finished.insert(idx);
            result.earlyReturn = result.earlyReturn || info.stepInfo.early_return;
            result.terminationRequested =
                result.terminationRequested || info.stepInfo.termination_requested;
            if (info.eventPending) {
                result.eventSources.insert(idx);
            }
        }
        return result;
    }

    algorithm::macro_step_result do_step_with_info_impl(time_point currentT)
    {
        std::unordered_set<simulator_index> participants;
        const auto participantLimit = prepare_step_participants(currentT, participants);
        if (eventRouter_.prepare_clock_step(currentT, participantLimit, clockRouter_, &participants)) {
            return {duration::zero(), {}, false, true};
        }
        const auto plannedEnd = currentT + participantLimit;
        auto requestedStepSize = clockRouter_.cap_step(
            currentT,
            participantLimit);
        if (eventRouter_.prepare_step_inputs(currentT, requestedStepSize, clockRouter_, &participants)) {
            return {duration::zero(), {}, false, true};
        }
        clockRouter_.stage_current_unclocked_data(participants);
        const bool clockStepCapped = requestedStepSize != stepSize_;
        const auto earlySources = step_early_sources(currentT, requestedStepSize, participants);
        if (auto result = early_termination_result(currentT, requestedStepSize, earlySources)) {
            return std::move(*result);
        }
        step_batch batch(pool_);
        submit_participant_steps(batch, currentT, requestedStepSize, participants, earlySources);
        auto results = collect_participant_steps(currentT, requestedStepSize, participants, earlySources);
        const auto endTime = results.endTime;
        auto& finished = results.finished;
        const auto& eventSources = results.eventSources;
        const bool earlyReturn = results.earlyReturn;
        bool terminationRequested = results.terminationRequested;

        const auto stepSizeTaken = requestedStepSize;
        const bool timeEvent = boost::algorithm::any_of(simulators_,
            [currentT, endTime](const auto& entry) {
                const auto next = entry.second.sim->get_event_handling_status().next_event_time;
                return next && *next > currentT && *next == endTime;
            });
        if (endTime < plannedEnd && (earlyReturn || timeEvent) && !interruptedEnd_) {
            interruptedEnd_ = plannedEnd;
        }
        if (interruptedEnd_ && endTime >= *interruptedEnd_) {
            interruptedEnd_.reset();
        }
        if (finished.size() != simulators_.size() && !energies_.empty()) {
            throw error(make_error_code(errc::unsupported_feature),
                "ECCO cannot evaluate power bonds while a simulator is ahead of the event time");
        }
        if (!terminationRequested && stepSizeTaken > duration::zero() && stepCounter_ >= 2) {
            stepSize_ = adjust_step_size(
                currentT, stepSizeTaken, params_,
                stepSizeTaken != stepSize_);
        }

        const bool simulatorEarlyReturn = earlyReturn;
        if (!terminationRequested) {
            terminationRequested = eventRouter_.finish_step_events(
                endTime, stepSize_, clockRouter_, eventSources, &finished);
        }
        for (const auto index : finished) {
            simulators_.at(index).eventPending = false;
        }
        if (!terminationRequested) {
            eventRouter_.require_step_progress(currentT, endTime);
        }

        // Transfer the outputs from simulators that have finished their
        // individual time steps within this co-simulation time step.
        if (!terminationRequested) {
            for (auto const simIndex : finished) {
                const auto& simInfo = simulators_.at(simIndex);
                transfer_variables(simInfo.outgoingSimConnections, false);
                transfer_variables(simInfo.outgoingFunConnections);
            }
            // Calculate functions and transfer their outputs.
            calculate_and_transfer_functions();
        }


        return {
            stepSizeTaken,
            std::move(finished),
            simulatorEarlyReturn || clockStepCapped,
            terminationRequested,
        };
    }

    std::pair<duration, std::unordered_set<simulator_index>> do_step(time_point currentT)
    {
        auto result = do_step_with_info(currentT);
        return {result.step_size, std::move(result.finished)};
    }

    serialization::node export_current_state() const
    {
        if (initializationFailed_) {
            throw error(make_error_code(errc::simulation_error),
                "The algorithm cannot continue after failed initialization; recreate the execution");
        }
        if (recoveryRequired_) {
            throw error(make_error_code(errc::simulation_error),
                "Cannot save algorithm state after a failed step; restore a consistent execution snapshot or recreate the execution");
        }
        check_state_supported();
        serialization::node state;
        state.put("type", std::string("ecco_algorithm"));
        state.put<std::uint32_t>("version", 4);
        state.put<std::int64_t>("step_counter", stepCounter_);
        state.put<std::int64_t>("step_size_ticks", stepSize_.count());
        state.put("has_interrupted_end", interruptedEnd_.has_value());
        if (interruptedEnd_) {
            state.put<std::int64_t>("interrupted_end_ticks", interruptedEnd_->time_since_epoch().count());
        }
        state.put("previous_error", prev_error_estimate_);
        state.put_child("event_router", eventRouter_.export_state());
        serialization::node coordination;
        for (const auto& [index, info] : simulators_) {
            serialization::node entry;
            entry.put("has_reached", info.reached.has_value());
            if (info.reached) {
                entry.put<std::int64_t>("reached_ticks", info.reached->time_since_epoch().count());
            }
            entry.put("event_pending", info.eventPending);
            coordination.put_child(std::to_string(index), entry);
        }
        state.put_child("coordination", coordination);
        if (clockRouter_.has_clock_support()) {
            state.put_child("clock_router", clockRouter_.export_state());
        }

        serialization::node parameters;
        parameters.put("safety_factor", params_.safety_factor);
        parameters.put<std::int64_t>("step_size_ticks", params_.step_size.count());
        parameters.put<std::int64_t>("min_step_size_ticks", params_.min_step_size.count());
        parameters.put<std::int64_t>("max_step_size_ticks", params_.max_step_size.count());
        parameters.put("min_change_rate", params_.min_change_rate);
        parameters.put("max_change_rate", params_.max_change_rate);
        parameters.put("abs_tolerance", params_.abs_tolerance);
        parameters.put("rel_tolerance", params_.rel_tolerance);
        parameters.put("p_gain", params_.p_gain);
        parameters.put("i_gain", params_.i_gain);
        state.put_child("parameters", parameters);

        serialization::node bonds;
        for (std::size_t i = 0; i < energies_.size(); ++i) {
            serialization::node bond;
            const auto putVariable = [&bond](const char* name, variable_id variable) {
                serialization::node node;
                node.put<std::int32_t>("simulator", variable.simulator);
                node.put<std::int32_t>("type", static_cast<std::int32_t>(variable.type));
                node.put<std::uint32_t>("reference", variable.reference);
                bond.put_child(name, node);
            };
            putVariable("input", inputVariables_.at(i));
            putVariable("output", outputVariables_.at(i));
            serialization::node history;
            const auto& bondEnergies = energies_.at(i);
            for (std::size_t j = 0; j < bondEnergies.size(); ++j) {
                history.put(std::to_string(j), bondEnergies.at(j));
            }
            bond.put_child("energies", history);
            bonds.put_child(std::to_string(i), bond);
        }
        state.put_child("bonds", bonds);
        return state;
    }

    std::uint32_t validate_state_configuration(const serialization::node& exportedState) const
    {
        const auto& parameters = exportedState.get_child("parameters");
        const auto version = exportedState.get<std::uint32_t>("version");
        if (exportedState.get<std::string>("type") != "ecco_algorithm" ||
            (version != 1 && version != 2 && version != 3 && version != 4) ||
            parameters.get<double>("safety_factor") != params_.safety_factor ||
            parameters.get<std::int64_t>("step_size_ticks") != params_.step_size.count() ||
            parameters.get<std::int64_t>("min_step_size_ticks") != params_.min_step_size.count() ||
            parameters.get<std::int64_t>("max_step_size_ticks") != params_.max_step_size.count() ||
            parameters.get<double>("min_change_rate") != params_.min_change_rate ||
            parameters.get<double>("max_change_rate") != params_.max_change_rate ||
            parameters.get<double>("abs_tolerance") != params_.abs_tolerance ||
            parameters.get<double>("rel_tolerance") != params_.rel_tolerance ||
            parameters.get<double>("p_gain") != params_.p_gain ||
            parameters.get<double>("i_gain") != params_.i_gain) {
            throw error(make_error_code(errc::bad_file),
                "The serialized ECCO configuration is invalid or incompatible");
        }
        return version;
    }

    std::vector<std::vector<double>> parse_state_energies(
        const serialization::node& exportedState) const
    {
        const auto& bonds = exportedState.get_child("bonds");
        if (bonds.size() != energies_.size()) {
            throw error(make_error_code(errc::bad_file),
                "The serialized ECCO power bonds are incompatible");
        }
        auto energies = std::vector<std::vector<double>>(energies_.size());
        for (std::size_t i = 0; i < energies.size(); ++i) {
            const auto& bond = bonds.get_child(std::to_string(i));
            const auto matches = [&bond](const char* name, variable_id variable) {
                const auto& node = bond.get_child(name);
                return node.get<std::int32_t>("simulator") == variable.simulator &&
                    node.get<std::int32_t>("type") == static_cast<std::int32_t>(variable.type) &&
                    node.get<std::uint32_t>("reference") == variable.reference;
            };
            if (!matches("input", inputVariables_.at(i)) ||
                !matches("output", outputVariables_.at(i))) {
                throw error(make_error_code(errc::bad_file),
                    "The serialized ECCO power bonds are incompatible");
            }
            const auto& history = bond.get_child("energies");
            energies.at(i).reserve(history.size());
            for (std::size_t j = 0; j < history.size(); ++j) {
                energies.at(i).push_back(history.get<double>(std::to_string(j)));
            }
        }
        return energies;
    }

    std::optional<time_point> parse_interrupted_end(
        const serialization::node& exportedState, std::uint32_t version) const
    {
        std::optional<time_point> interruptedEnd;
        if (version >= 4) {
            if (exportedState.get<bool>("has_interrupted_end")) {
                interruptedEnd = time_point(duration(exportedState.get<std::int64_t>("interrupted_end_ticks")));
                if (*interruptedEnd <= startTime_) {
                    throw error(make_error_code(errc::bad_file),
                        "The serialized ECCO interrupted endpoint is invalid");
                }
            } else if (exportedState.get_optional<std::int64_t>("interrupted_end_ticks")) {
                throw error(make_error_code(errc::bad_file),
                    "The serialized ECCO interrupted endpoint is invalid");
            }
        }
        return interruptedEnd;
    }

    using coordination_state =
        std::unordered_map<simulator_index, std::pair<std::optional<time_point>, bool>>;

    coordination_state parse_coordination_state(
        const serialization::node& exportedState, std::uint32_t version,
        const std::optional<time_point>& interruptedEnd) const
    {
        coordination_state coordination;
        const auto coordinationState = exportedState.get_child_optional("coordination");
        if (version >= 3 || coordinationState) {
            if (!coordinationState) {
                throw error(make_error_code(errc::bad_file),
                    "The serialized ECCO participant state is missing");
            }
            const auto& entries = *coordinationState;
            if (entries.size() != simulators_.size()) {
                throw error(make_error_code(errc::bad_file),
                    "The serialized ECCO participant state is incompatible");
            }
            for (const auto& [index, info] : simulators_) {
                const auto& entry = entries.get_child(std::to_string(index));
                std::optional<time_point> reached;
                if (entry.get<bool>("has_reached")) {
                    reached = time_point(duration(entry.get<std::int64_t>("reached_ticks")));
                } else if (entry.get_optional<std::int64_t>("reached_ticks")) {
                    throw error(make_error_code(errc::bad_file),
                        "The serialized ECCO participant time is invalid");
                }
                const bool pending = entry.get<bool>("event_pending");
                if (pending && !reached) {
                    throw error(make_error_code(errc::bad_file),
                        "The serialized ECCO participant event is invalid");
                }
                coordination.emplace(index, std::make_pair(reached, pending));
            }
        }
        if (interruptedEnd &&
            boost::algorithm::none_of(coordination, [&](const auto& entry) {
                return entry.second.first && *entry.second.first < *interruptedEnd;
            })) {
            throw error(make_error_code(errc::bad_file),
                "The serialized ECCO interrupted endpoint is incompatible with participant times");
        }
        return coordination;
    }

    void import_state(const serialization::node& exportedState)
    {
        if (initializationFailed_) {
            throw error(make_error_code(errc::simulation_error),
                "The algorithm cannot continue after failed initialization; recreate the execution");
        }
        check_state_supported();
        try {
            const auto version = validate_state_configuration(exportedState);
            const auto counter = exportedState.get<std::int64_t>("step_counter");
            const auto stepTicks = exportedState.get<std::int64_t>("step_size_ticks");
            const auto previousError = exportedState.get<double>("previous_error");
            if (counter < 0 || stepTicks <= 0 ||
                !std::isfinite(previousError) || previousError < 0.0) {
                throw error(make_error_code(errc::bad_file),
                    "The serialized ECCO controller state is invalid");
            }

            const auto interruptedEnd = parse_interrupted_end(exportedState, version);
            const auto coordination = parse_coordination_state(exportedState, version, interruptedEnd);
            auto energies = parse_state_energies(exportedState);
            auto restoredClockRouter = clockRouter_;
            const auto clockState = exportedState.get_child_optional("clock_router");
            if (clockRouter_.has_clock_support()) {
                if (version < 2 || !clockState) {
                    throw error(make_error_code(errc::unsupported_feature),
                        "The snapshot does not contain Clock coordination state");
                }
                restoredClockRouter.import_state(*clockState);
            } else if (clockState) {
                throw error(make_error_code(errc::bad_file),
                    "The snapshot contains incompatible Clock coordination state");
            }
            eventRouter_.import_state(exportedState.get_child("event_router"));
            clockRouter_ = std::move(restoredClockRouter);
            stepCounter_ = counter;
            stepSize_ = duration(stepTicks);
            interruptedEnd_ = interruptedEnd;
            prev_error_estimate_ = previousError;
            energies_ = std::move(energies);
            for (auto& [index, info] : simulators_) {
                const auto found = coordination.find(index);
                info.reached = found == coordination.end() ? std::nullopt : found->second.first;
                info.eventPending = found != coordination.end() && found->second.second;
                info.stepInfo = {};
            }
            recoveryRequired_ = false;
        } catch (const boost::property_tree::ptree_error&) {
            throw error(make_error_code(errc::bad_file),
                "The serialized ECCO state is invalid or corrupt");
        } catch (const std::bad_variant_access&) {
            throw error(make_error_code(errc::bad_file),
                "The serialized ECCO state is invalid or corrupt");
        }
    }

    void get_energies()
    {
        for (std::size_t i = 0; i < energies_.size(); ++i) {
            std::cout << "Avg energy for sim idx " << i << ": " << get_mean(energies_.at(i)) << std::endl; // NOLINT(performance-avoid-endl): Preserve the existing flush.
        }
    }

    duration adjust_step_size(
        time_point currentTime,
        const duration& stepSize,
        const ecco_algorithm_params& params,
        bool eventBoundaryLimited)
    {
        std::vector<double> power_residuals{};
        const std::vector<double> power{};

        const auto elapsed = to_double_duration(stepSize, currentTime);

        for (std::size_t i = 0; i < inputVariables_.size(); i += 2) {
            const auto input_a = inputVariables_.at(i);
            const auto input_b = inputVariables_.at(i + 1);
            const auto output_a = outputVariables_.at(i);
            const auto output_b = outputVariables_.at(i + 1);

            double const input_a_value = simulators_.at(input_a.simulator).sim->get_real(input_a.reference);
            double const input_b_value = simulators_.at(input_b.simulator).sim->get_real(input_b.reference);
            double const output_a_value = simulators_.at(output_a.simulator).sim->get_real(output_a.reference);
            double const output_b_value = simulators_.at(output_b.simulator).sim->get_real(output_b.reference);

            double const power_a = input_a_value * output_a_value;
            energies_.at(i).push_back(power_a * elapsed);

            double const power_b = input_b_value * output_b_value;
            energies_.at(i + 1).push_back(power_b * elapsed);

            double const power_residual = std::abs(power_a - power_b);

            power_residuals.push_back(power_residual);
        }

        // A scheduled boundary is not an adaptive step-size decision.
        if (eventBoundaryLimited) {
            return stepSize_;
        }

        if (power_residuals.empty()) {
            return stepSize;
        }

        double const max_power_residual = *boost::range::max_element(power_residuals);
        const auto energy_level = max_power_residual * elapsed;
        double mean_square{};
        for (auto const power_residual : power_residuals) {
            const auto energy_residual = power_residual * elapsed;
            mean_square += std::pow(energy_residual / (params.abs_tolerance + (params.rel_tolerance * energy_level)), 2);
        }
        const auto num_bonds = power_residuals.size(); // TODO: Still valid for multidimensial bonds?
        const auto error_estimate = std::sqrt(mean_square / static_cast<double>(num_bonds));

        if (prev_error_estimate_ == 0 || error_estimate == 0) {
            prev_error_estimate_ = error_estimate;
            return stepSize;
        }

        // Compute a new step size
        const auto new_step_size_gain_value = params.safety_factor * std::pow(error_estimate, -params.i_gain - params.p_gain) * std::pow(prev_error_estimate_, params.p_gain);
        auto const new_step_size_gain = std::clamp(new_step_size_gain_value, params.min_change_rate, params.max_change_rate);

        prev_error_estimate_ = error_estimate;
        const auto new_step_size = to_duration(new_step_size_gain * to_double_duration(stepSize, currentTime));
        const auto actual_new_step_size = std::clamp(new_step_size, params.min_step_size, params.max_step_size);
        return actual_new_step_size;
    }

    void add_power_bond(cosim::variable_id input_a, cosim::variable_id output_a, cosim::variable_id input_b, cosim::variable_id output_b)
    {
        simulators_.at(input_a.simulator).sim->expose_for_getting(input_a.type, input_a.reference);
        simulators_.at(output_a.simulator).sim->expose_for_getting(output_a.type, output_a.reference);
        simulators_.at(input_b.simulator).sim->expose_for_getting(input_b.type, input_b.reference);
        simulators_.at(output_b.simulator).sim->expose_for_getting(output_b.type, output_b.reference);
        energies_.emplace_back();
        energies_.emplace_back();
        inputVariables_.push_back(input_a);
        outputVariables_.push_back(output_a);
        inputVariables_.push_back(input_b);
        outputVariables_.push_back(output_b);
    }

    std::vector<double> get_powerbond_energies(cosim::simulator_index simulator_index)
    {
        return energies_.at(simulator_index);
    }

private:
    struct simulator_info;
    struct source_step
    {
        simulator_index index;
        simulator_info* info;
        std::optional<simulator::state_index> checkpoint;
        time_point endpoint;
        bool stepped = false;
    };

    static void release_early_checkpoints(const std::vector<source_step>& sources)
    {
        for (const auto& source : sources) {
            if (!source.checkpoint) {
                continue;
            }
            try {
                source.info->sim->release_state(*source.checkpoint);
            } catch (const std::exception& e) {
                BOOST_LOG_SEV(log::logger(), log::warning)
                    << source.info->sim->name()
                    << ": could not release an early-return checkpoint: " << e.what();
            }
        }
    }

    static void step_early_source(source_step& source, time_point currentT, time_point target)
    {
        auto* sim = source.info->sim;
        source.info->stepInfo = target == currentT
            ? step_result_info{step_result::complete, currentT}
            : sim->do_step_with_info(currentT, target - currentT);
        const auto& result = source.info->stepInfo;
        if (result.result != step_result::complete ||
            (result.early_return &&
                (result.last_successful_time < currentT ||
                    result.last_successful_time > target))) {
            throw error(make_error_code(errc::simulation_error),
                sim->name() + ": invalid or incomplete step during early-return coordination");
        }
        source.endpoint = step_endpoint(result, target);
        source.stepped = true;
    }

    void correct_early_overshoots(std::vector<source_step>& sources,
        time_point currentT, time_point& horizon, std::size_t& corrections)
    {
        while (true) {
            bool corrected = false;
            for (auto& previous : sources) {
                if (!previous.stepped || previous.endpoint <= horizon) {
                    continue;
                }
                if (!previous.checkpoint) {
                    BOOST_LOG_SEV(log::logger(), log::warning)
                        << previous.info->sim->name()
                        << " cannot be rewound to the event at "
                        << to_double_time_point(horizon) << " s; it has reached "
                        << to_double_time_point(previous.endpoint)
                        << " s; the event reaches it at its next communication point";
                    continue;
                }
                if (++corrections > eventRouter_.budget().limit()) {
                    throw error(make_error_code(errc::simulation_error),
                        "Early-return endpoints did not settle within the iteration limit");
                }
                previous.info->sim->restore_state(*previous.checkpoint);
                step_early_source(previous, currentT, horizon);
                if (previous.endpoint < horizon) {
                    horizon = previous.endpoint;
                    corrected = true;
                }
            }
            if (!corrected) {
                break;
            }
        }
    }

    std::unordered_set<simulator_index> step_early_sources(
        time_point currentT, duration& stepSize,
        const std::unordered_set<simulator_index>& participants)
    {
        std::vector<source_step> sources;
        for (auto& entry : simulators_) {
            if (participants.count(entry.first) != 0 &&
                entry.second.capabilities.might_return_early) {
                sources.push_back({entry.first, &entry.second, std::nullopt, currentT});
            }
        }
        if (sources.empty()) {
            return {};
        }

        const auto releaseStates = gsl::finally([&sources] {
            release_early_checkpoints(sources);
        });
        if (sources.size() > 1) {
            for (auto& source : sources) {
                save_step_checkpoint(*source.info->sim, source.info->capabilities.can_save_state,
                    stepSize, source.checkpoint);
            }
        }
        boost::range::stable_sort(sources, [](const auto& left, const auto& right) {
            return left.checkpoint.has_value() && !right.checkpoint.has_value();
        });

        auto horizon = currentT + stepSize;
        std::size_t corrections = 0;
        for (auto& source : sources) {
            step_early_source(source, currentT, horizon);
            horizon = std::min(horizon, source.endpoint);
            correct_early_overshoots(sources, currentT, horizon, corrections);
            if (source.info->stepInfo.termination_requested) {
                break;
            }
        }
        stepSize = horizon - currentT;
        std::unordered_set<simulator_index> result;
        for (const auto& source : sources) {
            if (!source.stepped) {
                continue;
            }
            source.info->reached = source.endpoint;
            source.info->eventPending = source.info->stepInfo.event_handling_needed;
            result.insert(source.index);
        }
        return result;
    }

    void check_state_supported() const
    {
        if (clockRouter_.termination_requested()) {
            throw error(make_error_code(errc::unsupported_feature),
                "Saving or restoring terminated ECCO event state is not supported");
        }
        if (!functions_.empty() ||
            boost::algorithm::any_of(simulators_, [](const auto& entry) {
                return entry.second.sim->has_variable_modifiers();
            })) {
            throw error(make_error_code(errc::unsupported_feature),
                "Saving or restoring ECCO with functions or variable modifiers is not supported");
        }
    }

    std::vector<cosim::variable_id> inputVariables_;
    std::vector<cosim::variable_id> outputVariables_;
    std::vector<std::vector<double>> energies_;

    static double get_mean(const std::vector<double>& elems)
    {
        if (elems.empty()) {
            return 0.0;
        }
        double sum{};
        for (auto const elem : elems) {
            sum += elem;
        }
        return sum / static_cast<double>(elems.size());
    }

    struct connection_ss
    {
        variable_id source;
        variable_id target;
        bool clocked = false;
    };

    struct connection_sf
    {
        variable_id source;
        function_io_id target;
    };

    struct connection_fs
    {
        function_io_id source;
        variable_id target;
    };

    struct connection_ff
    {
        function_io_id source;
        function_io_id target;
    };

    struct simulator_info
    {
        simulator* sim = nullptr;
        simulator_capabilities capabilities;
        cosim::step_result_info stepInfo;
        std::optional<time_point> reached;
        bool eventPending = false;
        std::vector<connection_ss> outgoingSimConnections;
        std::vector<connection_sf> outgoingFunConnections;
    };

    struct function_info
    {
        function* fun = nullptr;
        std::vector<connection_fs> outgoingSimConnections;
        std::vector<connection_ff> outgoingFunConnections;
    };

    void disconnect_simulator_variables(simulator_index index)
    {
        for (auto& simulator : simulators_) {
            auto& connections = simulator.second.outgoingSimConnections;
            const auto newEnd = boost::range::remove_if(
                connections,
                [index](const auto& connection) { return connection.target.simulator == index; });
            connections.erase(newEnd, connections.end());
        }
        for (auto& function : functions_) {
            auto& connections = function.second.outgoingSimConnections;
            const auto newEnd = boost::range::remove_if(
                connections,
                [index](const auto& connection) { return connection.target.simulator == index; });
            connections.erase(newEnd, connections.end());
        }
    }

    void transfer_variables(
        const std::vector<connection_ss>& connections,
        bool includeClockedData = true)
    {
        for (const auto& connection : connections) {
            if (connection.clocked && !includeClockedData) {
                continue;
            }
            transfer_variable(connection);
        }
    }

    void transfer_variables(const std::vector<connection_sf>& connections)
    {
        for (const auto& connection : connections) {
            transfer_variable(connection);
        }
    }

    void transfer_variables(const std::vector<connection_fs>& connections)
    {
        for (const auto& connection : connections) {
            transfer_variable(connection);
        }
    }

    void transfer_variables(const std::vector<connection_ff>& connections)
    {
        for (const auto& connection : connections) {
            transfer_variable(connection);
        }
    }

    void calculate_and_transfer_functions()
    {
        std::unordered_map<function_index, std::size_t> indegrees;
        for (const auto& function : functions_) {
            indegrees.emplace(function.first, 0);
        }
        for (const auto& function : functions_) {
            for (const auto& connection : function.second.outgoingFunConnections) {
                ++indegrees.at(connection.target.function);
            }
        }

        std::vector<function_index> ready;
        for (const auto& entry : indegrees) {
            if (entry.second == 0) {
                ready.push_back(entry.first);
            }
        }
        std::size_t processed = 0;
        while (!ready.empty()) {
            const auto index = ready.back();
            ready.pop_back();
            const auto& info = functions_.at(index);
            info.fun->calculate();
            transfer_variables(info.outgoingSimConnections);
            transfer_variables(info.outgoingFunConnections);
            ++processed;
            for (const auto& connection : info.outgoingFunConnections) {
                auto& indegree = indegrees.at(connection.target.function);
                if (--indegree == 0) {
                    ready.push_back(connection.target.function);
                }
            }
        }
        if (processed != functions_.size()) {
            throw std::logic_error(
                "Cyclic function-to-function connections are not supported");
        }
    }

    void transfer_variable(const connection_ss& connection)
    {
        assert(connection.source.type == connection.target.type);
        const auto value = simulators_.at(connection.source.simulator)
                               .sim->get_variable_value(
                                   connection.source.type, connection.source.reference);
        simulators_.at(connection.target.simulator)
            .sim->set_variable_value(
                connection.target.type, connection.target.reference, value);
    }

    void transfer_variable(const connection_sf& connection)
    {
        assert(connection.source.type == connection.target.type);
        const auto value = simulators_.at(connection.source.simulator)
                               .sim->get_variable_value(
                                   connection.source.type, connection.source.reference);
        functions_.at(connection.target.function)
            .fun->set_variable_value(connection.target.reference, value);
    }

    void transfer_variable(const connection_fs& connection)
    {
        assert(connection.source.type == connection.target.type);
        const auto value = functions_.at(connection.source.function)
                               .fun->get_variable_value(connection.source.reference);
        simulators_.at(connection.target.simulator)
            .sim->set_variable_value(
                connection.target.type, connection.target.reference, value);
    }

    void transfer_variable(const connection_ff& connection)
    {
        assert(connection.source.type == connection.target.type);
        const auto value = functions_.at(connection.source.function)
                               .fun->get_variable_value(connection.source.reference);
        functions_.at(connection.target.function)
            .fun->set_variable_value(connection.target.reference, value);
    }

    const ecco_algorithm_params params_;
    duration stepSize_;
    std::optional<time_point> interruptedEnd_;
    time_point startTime_;
    std::optional<time_point> stopTime_;
    std::unordered_map<simulator_index, simulator_info> simulators_;
    std::unordered_map<function_index, function_info> functions_;
    clock_router clockRouter_;
    event_router eventRouter_;
    int64_t stepCounter_ = 0;
    bool initialized_ = false;
    bool initializationFailed_ = false;
    bool recoveryRequired_ = false;
    unsigned int max_threads_ = std::thread::hardware_concurrency() - 1;
    utility::thread_pool pool_;
    double prev_error_estimate_{1.0};
};


ecco_algorithm::ecco_algorithm(ecco_algorithm_params params, std::optional<unsigned int> workerThreadCount)
    : pimpl_(std::make_unique<impl>(params, workerThreadCount))
{
}


ecco_algorithm::~ecco_algorithm() noexcept = default;


ecco_algorithm::ecco_algorithm(ecco_algorithm&& other) noexcept
    : pimpl_(std::move(other.pimpl_))
{
}


ecco_algorithm& ecco_algorithm::operator=(
    ecco_algorithm&& other) noexcept
{
    pimpl_ = std::move(other.pimpl_);
    return *this;
}


void ecco_algorithm::add_simulator(
    simulator_index index,
    simulator* sim,
    duration stepSizeHint)
{
    pimpl_->add_simulator(index, sim, stepSizeHint);
}

void ecco_algorithm::set_max_event_iterations(std::size_t limit)
{
    pimpl_->set_max_event_iterations(limit);
}

duration ecco_algorithm::initial_step_size() const noexcept { return pimpl_->initial_step_size(); }
std::size_t ecco_algorithm::worker_thread_count() const noexcept { return pimpl_->worker_thread_count(); }
std::size_t ecco_algorithm::max_event_iterations() const noexcept { return pimpl_->max_event_iterations(); }

void ecco_algorithm::set_event_value_tolerance(cosim::event_value_tolerance tolerance)
{
    pimpl_->set_event_value_tolerance(tolerance);
}

cosim::event_value_tolerance ecco_algorithm::event_value_tolerance() const noexcept
{
    return pimpl_->event_value_tolerance();
}


void ecco_algorithm::remove_simulator(simulator_index index)
{
    pimpl_->remove_simulator(index);
}


void ecco_algorithm::add_function(function_index index, function* fun)
{
    pimpl_->add_function(index, fun);
}

void ecco_algorithm::connect_variables(variable_id output, variable_id input)
{
    pimpl_->connect_variables(output, input);
}

void ecco_algorithm::connect_variables(variable_id output, function_io_id input)
{
    pimpl_->connect_variables(output, input);
}

void ecco_algorithm::connect_variables(function_io_id output, variable_id input)
{
    pimpl_->connect_variables(output, input);
}

void ecco_algorithm::connect_variables(
    function_io_id output, function_io_id input)
{
    pimpl_->connect_variables(output, input);
}

void ecco_algorithm::disconnect_variable(variable_id input)
{
    pimpl_->disconnect_variable(input);
}

void ecco_algorithm::disconnect_variable(function_io_id input)
{
    pimpl_->disconnect_variable(input);
}


void ecco_algorithm::setup(
    time_point startTime,
    std::optional<time_point> stopTime)
{
    pimpl_->setup(startTime, stopTime);
}


void ecco_algorithm::initialize()
{
    pimpl_->initialize();
}


std::pair<duration, std::unordered_set<simulator_index>> ecco_algorithm::do_step(
    time_point currentT)
{
    return pimpl_->do_step(currentT);
}

algorithm::macro_step_result ecco_algorithm::do_step_with_info(
    time_point currentT)
{
    return pimpl_->do_step_with_info(currentT);
}

serialization::node ecco_algorithm::export_current_state() const
{
    return pimpl_->export_current_state();
}

void ecco_algorithm::import_state(const serialization::node& exportedState)
{
    pimpl_->import_state(exportedState);
}

void ecco_algorithm::add_power_bond(cosim::variable_id input_a, cosim::variable_id output_a, cosim::variable_id input_b, cosim::variable_id output_b)
{
    pimpl_->add_power_bond(input_a, output_a, input_b, output_b);
}

std::vector<double> ecco_algorithm::get_powerbond_energies(cosim::simulator_index simulator_index)
{
    return pimpl_->get_powerbond_energies(simulator_index);
}

} // namespace cosim
