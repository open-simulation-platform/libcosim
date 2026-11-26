/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "fmu_internal.hpp"

#include "cosim/exception.hpp"
#include "cosim/fmi/v3/fmu.hpp"
#include "cosim/model_description.hpp"
#include "cosim/serialization.hpp"
#include "cosim/slave.hpp"
#include "cosim/time.hpp"

#include <boost/property_tree/exceptions.hpp>
#include <boost/range/algorithm/sort.hpp>
#include <gsl/util>

#include <FMI3/fmi3PlatformTypes.h>
#include <FMI3/fmi3_import_capi.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace cosim::fmi::v3
{

using adapter_detail::check_available;
using adapter_detail::check_callback_reentrancy;
using adapter_detail::check_status;
using adapter_detail::operation_access;
using adapter_detail::representable_time;

slave::state_index slave_instance::save_state()
{
    check_callback_reentrancy(callbackContext_.get(), instanceName_,
        "GetFMUState");
    std::scoped_lock const lock(mutex_);
    saved_state currentState;
    const auto releaseUnstoredState = gsl::finally([&] {
        if (currentState.fmuState && !callbackContext_->fmu_returned_fatal()) {
            callbackContext_->record_status(
                fmi3_import_free_fmu_state(handle_, &currentState.fmuState));
        }
    });
    copy_current_state(currentState);
    const auto index = store_new_state(std::move(currentState));
    currentState.fmuState = nullptr; // NOLINT(bugprone-use-after-move)
    return index;
}

void slave_instance::save_state(state_index stateIndex)
{
    check_callback_reentrancy(callbackContext_.get(), instanceName_,
        "GetFMUState");
    std::scoped_lock const lock(mutex_);
    if (savedStates_.at(stateIndex).fmuState == nullptr) {
        throw error(make_error_code(errc::model_error),
            instanceName_ + ": saved state has already been released");
    }
    copy_current_state(savedStates_.at(stateIndex));
}

void slave_instance::restore_state(state_index stateIndex)
{
    check_callback_reentrancy(callbackContext_.get(), instanceName_,
        "SetFMUState");
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "SetFMUState",
        operation_access::recovery);
    auto state = savedStates_.at(stateIndex);
    if (state.fmuState == nullptr) {
        throw error(make_error_code(errc::model_error),
            instanceName_ + ": saved state has been released");
    }
    const auto status = fmi3_import_set_fmu_state(handle_, state.fmuState);
    check_status(callbackContext_.get(), "SetFMUState", status);
    callbackContext_->clear_recovery();
    setupComplete_ = state.setupComplete;
    simStarted_ = state.simStarted;
    state_ = state.lifecycleState;
    stateBeforeConfiguration_ = state.stateBeforeConfiguration;
    currentTime_ = state.currentTime;
    stopTime_ = state.stopTime;
    terminationRequested_ = state.terminationRequested;
    eventHandlingRequested_ = state.eventHandlingRequested;
    if (terminationRequested_) {
        callbackContext_->require_recovery(true);
    }
    terminationAttempted_ = false;
    stepSetStarted_ = state.stepSetStarted;
    nextEventTime_ = state.nextEventTime;
    eventIterationComplete_ = state.eventIterationComplete;
    activeClocks_.swap(state.activeClocks);
    activatedPeriodicClocks_.swap(state.activatedPeriodicClocks);
    changingClocksAwaitingInterval_.swap(state.changingClocksAwaitingInterval);
    resolvedInputClockSchedules_.swap(state.resolvedInputClockSchedules);
    configuredFixedClockSchedules_.swap(state.configuredFixedClockSchedules);
    initialClockIntervals_.swap(state.initialClockIntervals);
    pendingClockIntervals_.swap(state.pendingClockIntervals);
    if (state.hasDimensionValues) {
        structuralParameterValues_.swap(state.dimensionValues);
    } else if (state_ != state::configuration) {
        // Legacy exports did not carry the instance's dimension cache.
        refresh_structural_parameters();
    }
}

void slave_instance::release_state(state_index stateIndex)
{
    check_callback_reentrancy(callbackContext_.get(), instanceName_,
        "FreeFMUState");
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "FreeFMUState",
        operation_access::recovery);
    auto& state = savedStates_.at(stateIndex);
    if (state.fmuState == nullptr) {
        throw error(make_error_code(errc::model_error),
            instanceName_ + ": saved state has already been released");
    }
    const auto status = fmi3_import_free_fmu_state(handle_, &state.fmuState);
    check_status(callbackContext_.get(), "FreeFMUState", status);
    state.fmuState = nullptr;
    savedStatesFreelist_.push(stateIndex);
}

constexpr std::int32_t export_scheme_version = 4;

serialization::node slave_instance::export_state(state_index stateIndex) const
{
    check_callback_reentrancy(callbackContext_.get(), instanceName_,
        "SerializeFMUState");
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "SerializeFMUState",
        operation_access::diagnostic);
    const auto& state = savedStates_.at(stateIndex);
    if (!fmu_->model_description()->capabilities.can_export_state) {
        throw error(make_error_code(errc::unsupported_feature),
            instanceName_ + ": FMU does not support state serialization");
    }
    if (state.fmuState == nullptr) {
        throw error(make_error_code(errc::model_error),
            instanceName_ + ": saved state has been released");
    }

    std::size_t stateSize = 0;
    auto status = fmi3_import_serialized_fmu_state_size(handle_, state.fmuState,
        &stateSize);
    check_status(callbackContext_.get(), "SerializedFMUStateSize", status);

    auto serializedState = std::vector<std::byte>(stateSize);
    status = fmi3_import_serialize_fmu_state(
        handle_, state.fmuState,
        reinterpret_cast<fmi3_byte_t*>(serializedState.data()), // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): FMI writes the serialized byte buffer.
        serializedState.size());
    check_status(callbackContext_.get(), "SerializeFMUState", status);

    serialization::node exportedState;
    exportedState.put("scheme_version", export_scheme_version);
    exportedState.put("interface", std::string("co_simulation"));
    exportedState.put("fmu_instantiation_token",
        fmu_->model_description()->instantiation_token);
    exportedState.put("serialized_fmu_state", serializedState);
    exportedState.put("setup_complete", state.setupComplete);
    exportedState.put("simulation_started", state.simStarted);
    exportedState.put("lifecycle_state", static_cast<int>(state.lifecycleState));
    exportedState.put("state_before_configuration",
        static_cast<int>(state.stateBeforeConfiguration));
    exportedState.put("current_time", to_double_time_point(state.currentTime));
    exportedState.put<std::int64_t>("current_time_ticks",
        state.currentTime.time_since_epoch().count());
    if (state.stopTime) {
        exportedState.put("stop_time", to_double_time_point(*state.stopTime));
        exportedState.put<std::int64_t>("stop_time_ticks",
            state.stopTime->time_since_epoch().count());
    }
    exportedState.put("termination_requested", state.terminationRequested);
    exportedState.put("event_handling_requested", state.eventHandlingRequested);
    exportedState.put("step_set_started", state.stepSetStarted);
    exportedState.put("event_iteration_complete", state.eventIterationComplete);
    if (state.nextEventTime) {
        exportedState.put("next_event_time",
            to_double_time_point(*state.nextEventTime));
        exportedState.put<std::int64_t>(
            "next_event_time_ticks",
            state.nextEventTime->time_since_epoch().count());
    }
    serialization::node activeClocks;
    for (const auto reference : state.activeClocks) {
        serialization::node entry;
        entry.put("reference", reference);
        activeClocks.push_back({"clock", entry});
    }
    exportedState.add_child("active_clocks", activeClocks);
    serialization::node activatedPeriodicClocks;
    for (const auto reference : state.activatedPeriodicClocks) {
        serialization::node entry;
        entry.put("reference", reference);
        activatedPeriodicClocks.push_back({"clock", entry});
    }
    exportedState.add_child("activated_periodic_clocks", activatedPeriodicClocks);
    serialization::node changingClocksAwaitingInterval;
    const std::set<value_reference> orderedChangingClocks(
        state.changingClocksAwaitingInterval.begin(),
        state.changingClocksAwaitingInterval.end());
    for (const auto reference : orderedChangingClocks) {
        serialization::node entry;
        entry.put("reference", reference);
        changingClocksAwaitingInterval.push_back({"clock", entry});
    }
    exportedState.add_child("changing_clocks_awaiting_interval",
        changingClocksAwaitingInterval);
    const auto encodeSchedules = [](const auto& schedules) {
        serialization::node resolvedClocks;
        std::vector<value_reference> resolvedReferences;
        resolvedReferences.reserve(schedules.size());
        for (const auto& entry : schedules) {
            resolvedReferences.push_back(entry.first);
        }
        boost::range::sort(resolvedReferences);
        for (const auto reference : resolvedReferences) {
            const auto& schedule = schedules.at(reference);
            serialization::node entry;
            entry.put("reference", reference);
            const bool fractional = schedule.interval_counter.has_value();
            entry.put("fractional", fractional);
            entry.put("interval_decimal", *schedule.interval_decimal);
            if (fractional) {
                entry.put("interval_counter", *schedule.interval_counter);
                entry.put("resolution", *schedule.resolution);
                entry.put("shift_counter", *schedule.shift_counter);
            } else {
                entry.put("shift_decimal", *schedule.shift_decimal);
            }
            resolvedClocks.push_back({"clock", entry});
        }
        return resolvedClocks;
    };
    exportedState.add_child("resolved_input_clocks",
        encodeSchedules(state.resolvedInputClockSchedules));
    exportedState.add_child("configured_input_clocks",
        encodeSchedules(state.configuredFixedClockSchedules));
    const auto encodeIntervals = [](const auto& intervals) {
        serialization::node pendingClocks;
        std::vector<value_reference> pendingReferences;
        pendingReferences.reserve(intervals.size());
        for (const auto& entry : intervals) {
            pendingReferences.push_back(entry.first);
        }
        boost::range::sort(pendingReferences);
        for (const auto reference : pendingReferences) {
            const auto& interval = intervals.at(reference);
            serialization::node entry;
            entry.put("reference", reference);
            entry.put("interval_decimal", interval.interval);
            entry.put("fractional", interval.counter.has_value());
            if (interval.counter) {
                entry.put("counter", *interval.counter);
                entry.put("resolution", *interval.resolution);
            }
            pendingClocks.push_back({"clock", entry});
        }
        return pendingClocks;
    };
    exportedState.add_child("pending_clock_intervals",
        encodeIntervals(state.pendingClockIntervals));
    exportedState.add_child("initial_clock_intervals",
        encodeIntervals(state.initialClockIntervals));
    if (state.hasDimensionValues) {
        serialization::node dimensions;
        const std::map<value_reference, std::uint64_t> ordered(
            state.dimensionValues.begin(), state.dimensionValues.end());
        for (const auto& [reference, value] : ordered) {
            serialization::node entry;
            entry.put("reference", reference);
            entry.put("value", value);
            dimensions.push_back({"dimension", entry});
        }
        exportedState.add_child("dimension_values", dimensions);
    }
    return exportedState;
}

void slave_instance::parse_state_lifecycle(
    const serialization::node& exportedState, int schemeVersion,
    saved_state& state) const
{
    state.setupComplete = exportedState.get<bool>("setup_complete");
    state.simStarted = exportedState.get<bool>("simulation_started");
    const auto lifecycleState =
        exportedState.get_optional<int>("lifecycle_state");
    if (lifecycleState) {
        if (*lifecycleState <
                static_cast<int>(slave_instance::state::instantiated) ||
            *lifecycleState >
                static_cast<int>(slave_instance::state::configuration)) {
            throw error(make_error_code(errc::bad_file),
                "The serialized state of subsimulator '" + instanceName_ +
                    "' contains an invalid lifecycle state");
        }
        state.lifecycleState = static_cast<slave_instance::state>(*lifecycleState);
    } else {
        if (state.simStarted) {
            state.lifecycleState = slave_instance::state::step;
        } else if (state.setupComplete) {
            state.lifecycleState = slave_instance::state::initialization;
        } else {
            state.lifecycleState = slave_instance::state::instantiated;
        }
    }
    const auto stateBeforeConfiguration =
        exportedState.get_optional<int>("state_before_configuration")
            .value_or(static_cast<int>(slave_instance::state::instantiated));
    if (stateBeforeConfiguration <
            static_cast<int>(slave_instance::state::instantiated) ||
        stateBeforeConfiguration >
            static_cast<int>(slave_instance::state::event) ||
        stateBeforeConfiguration ==
            static_cast<int>(slave_instance::state::initialization)) {
        throw error(make_error_code(errc::bad_file),
            "The serialized state of subsimulator '" + instanceName_ +
                "' contains an invalid Configuration Mode predecessor");
    }
    state.stateBeforeConfiguration =
        static_cast<slave_instance::state>(stateBeforeConfiguration);
    parse_state_timing_and_event_flags(exportedState, schemeVersion, state);
}

void slave_instance::parse_state_timing_and_event_flags(
    const serialization::node& exportedState, int schemeVersion,
    saved_state& state) const
{
    const auto currentTime = exportedState.get<double>("current_time");
    if (!std::isfinite(currentTime) ||
        (schemeVersion == 1 && !representable_time(currentTime))) {
        throw error(make_error_code(errc::bad_file),
            "The serialized state of subsimulator '" + instanceName_ +
                "' contains an invalid current time");
    }
    state.currentTime =
        schemeVersion >= 2
        ? time_point(
              duration(exportedState.get<std::int64_t>("current_time_ticks")))
        : to_time_point(currentTime);
    if (schemeVersion >= 2 &&
        to_double_time_point(state.currentTime) != currentTime) {
        throw error(make_error_code(errc::bad_file),
            "The serialized state of subsimulator '" + instanceName_ +
                "' contains inconsistent current times");
    }
    if (const auto stopTime = exportedState.get_optional<double>("stop_time")) {
        if (!std::isfinite(*stopTime) ||
            (schemeVersion == 1 && !representable_time(*stopTime))) {
            throw error(make_error_code(errc::bad_file),
                "The serialized state of subsimulator '" + instanceName_ +
                    "' contains an invalid stop time");
        }
        state.stopTime =
            schemeVersion >= 2
            ? time_point(
                  duration(exportedState.get<std::int64_t>("stop_time_ticks")))
            : to_time_point(*stopTime);
        if (*state.stopTime < state.currentTime ||
            (schemeVersion >= 2 &&
                to_double_time_point(*state.stopTime) != *stopTime)) {
            throw error(make_error_code(errc::bad_file),
                "The serialized state of subsimulator '" + instanceName_ +
                    "' contains an invalid stop time");
        }
    } else if (schemeVersion >= 2 &&
        exportedState.get_optional<std::int64_t>("stop_time_ticks")) {
        throw error(make_error_code(errc::bad_file),
            "The serialized state of subsimulator '" + instanceName_ +
                "' contains inconsistent stop times");
    }
    state.terminationRequested =
        exportedState.get_optional<bool>("termination_requested").value_or(false);
    state.eventHandlingRequested =
        exportedState.get_optional<bool>("event_handling_requested")
            .value_or(false);
    state.stepSetStarted =
        exportedState.get_optional<bool>("step_set_started").value_or(false);
    state.eventIterationComplete =
        exportedState.get_optional<bool>("event_iteration_complete")
            .value_or(false);
    parse_state_next_event_time(exportedState, schemeVersion, state);
}

void slave_instance::parse_state_next_event_time(
    const serialization::node& exportedState, int schemeVersion,
    saved_state& state) const
{
    if (const auto nextEventTime =
            exportedState.get_optional<double>("next_event_time")) {
        if (!std::isfinite(*nextEventTime) ||
            (schemeVersion == 1 && !representable_time(*nextEventTime))) {
            throw error(make_error_code(errc::bad_file),
                "The serialized state of subsimulator '" + instanceName_ +
                    "' contains an invalid next event time");
        }
        state.nextEventTime =
            schemeVersion >= 2
            ? time_point(duration(
                  exportedState.get<std::int64_t>("next_event_time_ticks")))
            : to_time_point(*nextEventTime);
        if (*state.nextEventTime < state.currentTime ||
            (state.stopTime && *state.nextEventTime > *state.stopTime) ||
            (schemeVersion >= 2 &&
                to_double_time_point(*state.nextEventTime) != *nextEventTime)) {
            throw error(make_error_code(errc::bad_file),
                "The serialized state of subsimulator '" + instanceName_ +
                    "' contains an invalid next event time");
        }
    } else if (schemeVersion >= 2 && exportedState.get_optional<std::int64_t>("next_event_time_ticks")) {
        throw error(make_error_code(errc::bad_file),
            "The serialized state of subsimulator '" + instanceName_ +
                "' contains inconsistent next event times");
    }
}

void slave_instance::parse_state_dimensions(
    const serialization::node& exportedState, saved_state& state) const
{
    if (const auto dimensions =
            exportedState.get_child_optional("dimension_values")) {
        state.hasDimensionValues = true;
        auto& values = state.dimensionValues;
        for (const auto& entry : *dimensions) {
            const auto reference = entry.second.get<value_reference>("reference");
            const auto value = entry.second.get<std::uint64_t>("value");
            const auto* variable =
                fmu_->find_variable(variable_type::uint64, reference);
            if (entry.first != "dimension" || (variable == nullptr) ||
                !variable->dimensions.empty() ||
                (variable->causality != variable_causality::structural_parameter &&
                    (variable->variability != variable_variability::constant ||
                        !variable->start ||
                        std::get<std::uint64_t>(*variable->start) != value)) ||
                !values.emplace(reference, value).second) {
                throw error(make_error_code(errc::bad_file),
                    "The serialized state contains invalid dimension values");
            }
        }
    }
}

void slave_instance::parse_state_clocks(
    const serialization::node& exportedState, int schemeVersion,
    saved_state& state) const
{
    if (const auto activeClocks =
            exportedState.get_child_optional("active_clocks")) {
        for (const auto& entry : *activeClocks) {
            if (entry.first != "clock") {
                throw error(make_error_code(errc::bad_file),
                    "The serialized state of subsimulator '" + instanceName_ +
                        "' contains an invalid active Clock entry");
            }
            const auto reference = entry.second.get<value_reference>("reference");
            if (fmu_->find_variable(variable_type::clock, reference) == nullptr ||
                !state.activeClocks.insert(reference).second) {
                throw error(make_error_code(errc::bad_file),
                    "The serialized state of subsimulator '" + instanceName_ +
                        "' contains an invalid active Clock");
            }
        }
    }
    if (schemeVersion >= 3 &&
        !exportedState.get_child_optional("activated_periodic_clocks")) {
        throw error(make_error_code(errc::bad_file),
            "The serialized state is missing periodic Clock bookkeeping");
    }
    if (const auto activatedPeriodicClocks =
            exportedState.get_child_optional("activated_periodic_clocks")) {
        for (const auto& entry : *activatedPeriodicClocks) {
            if (entry.first != "clock") {
                throw error(make_error_code(errc::bad_file),
                    "The serialized state of subsimulator '" + instanceName_ +
                        "' contains an invalid periodic Clock entry");
            }
            const auto reference = entry.second.get<value_reference>("reference");
            const auto description =
                std::find_if(clock_descriptions().begin(), clock_descriptions().end(),
                    [reference](const auto& clock) {
                        return clock.reference == reference;
                    });
            if (description == clock_descriptions().end() ||
                description->causality != variable_causality::input ||
                (description->interval_variability !=
                        clock_interval_variability::constant &&
                    description->interval_variability !=
                        clock_interval_variability::fixed &&
                    description->interval_variability !=
                        clock_interval_variability::tunable) ||
                !state.activatedPeriodicClocks.insert(reference).second) {
                throw error(make_error_code(errc::bad_file),
                    "The serialized state of subsimulator '" + instanceName_ +
                        "' contains an invalid periodic Clock");
            }
        }
    }
    if (schemeVersion >= 3) {
        parse_state_clock_schedules(exportedState, schemeVersion, state);
    }
    parse_state_changing_clocks(exportedState, state);
}

void slave_instance::parse_state_clock_schedules(
    const serialization::node& exportedState, int schemeVersion,
    saved_state& state) const
{
    parse_state_clock_schedule_entries(exportedState.get_child("resolved_input_clocks"),
        state.resolvedInputClockSchedules, false);
    if (const auto clocks =
            exportedState.get_child_optional("configured_input_clocks")) {
        parse_state_clock_schedule_entries(*clocks, state.configuredFixedClockSchedules, true);
    }
    parse_state_initial_clock_intervals(exportedState, state);
    validate_state_clock_schedules(state);
    if (schemeVersion >= 4) {
        parse_state_pending_clock_intervals(exportedState, state);
    }
}

void slave_instance::parse_state_clock_schedule_entries(
    const serialization::node& entries,
    std::unordered_map<value_reference, cosim::clock_description>& schedules,
    bool configured) const
{
    for (const auto& entry : entries) {
        parse_state_clock_schedule_entry(entry, schedules, configured);
    }
}

void slave_instance::parse_state_clock_schedule_entry(
    const serialization::node::value_type& entry,
    std::unordered_map<value_reference, cosim::clock_description>& schedules,
    bool configured) const
{
    if (entry.first != "clock") {
        throw error(
            make_error_code(errc::bad_file),
            "The serialized state contains an invalid resolved Clock entry");
    }
    const auto reference = entry.second.get<value_reference>("reference");
    const auto* variable =
        fmu_->find_variable(variable_type::clock, reference);
    const bool fractional = entry.second.get<bool>("fractional");
    const bool changing =
        (variable != nullptr) && variable->clock &&
        (variable->clock->interval_variability ==
                cosim::clock_interval_variability::changing ||
            variable->clock->interval_variability ==
                cosim::clock_interval_variability::countdown);
    if ((variable == nullptr) || !variable->clock ||
        variable->causality != variable_causality::input ||
        (variable->clock->interval_variability !=
                cosim::clock_interval_variability::fixed &&
            variable->clock->interval_variability !=
                cosim::clock_interval_variability::tunable &&
            !changing) ||
        (fractional && !variable->clock->supports_fraction) ||
        (configured && (changing || !variable->clock->interval_decimal))) {
        throw error(
            make_error_code(errc::bad_file),
            "The serialized state contains an incompatible resolved Clock");
    }
    auto schedule = *variable->clock;
    const auto interval = entry.second.get<double>("interval_decimal");
    if (!std::isfinite(interval) || interval < 0.0 ||
        (!changing && interval == 0.0)) {
        throw error(
            make_error_code(errc::bad_file),
            "The serialized state contains an invalid Clock interval");
    }
    schedule.interval_decimal = interval;
    if (fractional) {
        const auto counter =
            entry.second.get<std::uint64_t>("interval_counter");
        const auto resolution = entry.second.get<std::uint64_t>("resolution");
        const auto shift = entry.second.get<std::uint64_t>("shift_counter");
        if ((!changing && counter == 0) || resolution == 0 ||
            (changing && shift != 0) ||
            static_cast<double>(counter) / static_cast<double>(resolution) !=
                interval) {
            throw error(make_error_code(errc::bad_file),
                "The serialized state contains an invalid fractional "
                "Clock schedule");
        }
        schedule.interval_counter = counter;
        schedule.resolution = resolution;
        schedule.shift_counter = shift;
        schedule.shift_decimal.reset();
    } else {
        const auto shift = entry.second.get<double>("shift_decimal");
        if (!std::isfinite(shift) || shift < 0.0 ||
            (changing && shift != 0.0)) {
            throw error(make_error_code(errc::bad_file),
                "The serialized state contains an invalid Clock shift");
        }
        schedule.shift_decimal = shift;
        schedule.interval_counter.reset();
        schedule.resolution.reset();
        schedule.shift_counter.reset();
    }
    if (!schedules.emplace(reference, schedule).second) {
        throw error(
            make_error_code(errc::bad_file),
            "The serialized state contains a duplicate resolved Clock");
    }
}

void slave_instance::parse_state_initial_clock_intervals(
    const serialization::node& exportedState, saved_state& state) const
{
    if (const auto intervals =
            exportedState.get_child_optional("initial_clock_intervals")) {
        for (const auto& entry : *intervals) {
            const auto reference = entry.second.get<value_reference>("reference");
            const auto* variable =
                fmu_->find_variable(variable_type::clock, reference);
            const auto interval = entry.second.get<double>("interval_decimal");
            const bool fractional = entry.second.get<bool>("fractional");
            if (state.lifecycleState != state::initialization ||
                entry.first != "clock" || (variable == nullptr) ||
                !variable->clock ||
                variable->causality != variable_causality::input ||
                fractional != variable->clock->supports_fraction ||
                !std::isfinite(interval) || interval < 0.0) {
                throw error(make_error_code(errc::bad_file),
                    "The serialized state contains an incompatible initial "
                    "Clock interval");
            }
            clock_interval_update update{
                clock_interval_qualifier::changed,
                interval,
                {},
                {},
            };
            if (fractional) {
                update.counter = entry.second.get<std::uint64_t>("counter");
                update.resolution = entry.second.get<std::uint64_t>("resolution");
                if (*update.resolution == 0 ||
                    static_cast<double>(*update.counter) /
                            static_cast<double>(*update.resolution) !=
                        interval) {
                    throw error(make_error_code(errc::bad_file),
                        "The serialized state contains an invalid initial "
                        "Clock fraction");
                }
            }
            if (!state.initialClockIntervals.emplace(reference, update).second) {
                throw error(make_error_code(errc::bad_file),
                    "The serialized state contains duplicate initial Clock "
                    "intervals");
            }
        }
    }
}

void slave_instance::validate_state_clock_schedules(const saved_state& state) const
{
    for (const auto& clock : fmu_->clock_descriptions()) {
        if (clock.causality == variable_causality::input &&
            ((clock.interval_variability == clock_interval_variability::fixed &&
                 !clock.interval_decimal) ||
                clock.interval_variability == clock_interval_variability::tunable ||
                clock.interval_variability ==
                    clock_interval_variability::changing) &&
            state.simStarted &&
            state.resolvedInputClockSchedules.count(clock.reference) == 0) {
            throw error(
                make_error_code(errc::bad_file),
                "The serialized state is missing a resolved Clock schedule");
        }
    }
    if (!state.simStarted && !state.resolvedInputClockSchedules.empty()) {
        throw error(
            make_error_code(errc::bad_file),
            "The serialized state contains unexpected resolved Clock schedules");
    }
}

void slave_instance::parse_state_pending_clock_intervals(
    const serialization::node& exportedState, saved_state& state) const
{
    for (const auto& entry :
        exportedState.get_child("pending_clock_intervals")) {
        if (entry.first != "clock") {
            throw error(make_error_code(errc::bad_file),
                "The serialized state contains an invalid pending Clock "
                "interval");
        }
        const auto reference = entry.second.get<value_reference>("reference");
        const auto* variable =
            fmu_->find_variable(variable_type::clock, reference);
        const auto resolved = state.resolvedInputClockSchedules.find(reference);
        const bool fractional = entry.second.get<bool>("fractional");
        const auto interval = entry.second.get<double>("interval_decimal");
        if (!state.simStarted || (variable == nullptr) || !variable->clock ||
            variable->causality != variable_causality::input ||
            variable->clock->interval_variability !=
                cosim::clock_interval_variability::tunable ||
            !variable->clock->interval_decimal ||
            resolved == state.resolvedInputClockSchedules.end() ||
            fractional != resolved->second.interval_counter.has_value() ||
            !std::isfinite(interval) ||
            static_cast<long double>(interval) * duration::period::den < duration{1}.count()) {
            throw error(make_error_code(errc::bad_file),
                "The serialized state contains an incompatible pending "
                "Clock interval");
        }
        clock_interval_update update{
            clock_interval_qualifier::changed,
            interval,
            {},
            {},
        };
        if (fractional) {
            update.counter = entry.second.get<std::uint64_t>("counter");
            update.resolution = entry.second.get<std::uint64_t>("resolution");
            if (*update.resolution == 0 ||
                *update.counter <
                    ((*update.resolution - 1) / duration::period::den) + 1 ||
                static_cast<double>(*update.counter) /
                        static_cast<double>(*update.resolution) !=
                    interval) {
                throw error(make_error_code(errc::bad_file),
                    "The serialized pending Clock fraction is invalid");
            }
        }
        if (!state.pendingClockIntervals.emplace(reference, update).second) {
            throw error(make_error_code(errc::bad_file),
                "The serialized state contains duplicate pending Clock "
                "intervals");
        }
    }
}

void slave_instance::parse_state_changing_clocks(
    const serialization::node& exportedState, saved_state& state) const
{
    if (const auto clocks = exportedState.get_child_optional(
            "changing_clocks_awaiting_interval")) {
        for (const auto& entry : *clocks) {
            const auto reference = entry.second.get<value_reference>("reference");
            const auto* variable =
                fmu_->find_variable(variable_type::clock, reference);
            if (entry.first != "clock" || (variable == nullptr) || !variable->clock ||
                variable->causality != variable_causality::input ||
                variable->clock->interval_variability !=
                    cosim::clock_interval_variability::changing ||
                !state.changingClocksAwaitingInterval.insert(reference).second) {
                throw error(make_error_code(errc::bad_file),
                    "The serialized state contains an invalid changing Clock");
            }
        }
    }
    const bool hasEventClockState =
        !state.activeClocks.empty() || !state.activatedPeriodicClocks.empty() ||
        !state.changingClocksAwaitingInterval.empty() ||
        state.eventIterationComplete;
    const bool hasEventLifecycle =
        state.lifecycleState == slave_instance::state::event ||
        (state.lifecycleState == slave_instance::state::configuration &&
            state.stateBeforeConfiguration == slave_instance::state::event);
    if (hasEventClockState && !hasEventLifecycle) {
        throw error(make_error_code(errc::bad_file),
            "The serialized state of subsimulator '" + instanceName_ +
                "' contains inconsistent Clock lifecycle data");
    }
}

slave::state_index
slave_instance::import_state(const serialization::node& exportedState)
{
    check_callback_reentrancy(callbackContext_.get(), instanceName_,
        "DeserializeFMUState");
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "DeserializeFMUState",
        operation_access::recovery);
    saved_state state;
    bool stateStored = false;
    const auto cleanupState = gsl::finally([&] {
        if (!stateStored && state.fmuState != nullptr &&
            !callbackContext_->fmu_returned_fatal()) {
            callbackContext_->record_status(
                fmi3_import_free_fmu_state(handle_, &state.fmuState));
        }
    });
    const auto corrupt = [&] {
        return error(make_error_code(errc::bad_file),
            "The serialized state of subsimulator '" + instanceName_ +
                "' is invalid or corrupt");
    };
    try {
        const auto schemeVersion =
            exportedState.get<std::int32_t>("scheme_version");
        if ((schemeVersion < 1 || schemeVersion > export_scheme_version) ||
            exportedState.get<std::string>("interface") != "co_simulation") {
            throw error(make_error_code(errc::bad_file),
                "The serialized state of subsimulator '" + instanceName_ +
                    "' uses an incompatible scheme");
        }

        const auto token = std::get<std::string>(
            exportedState.get_child("fmu_instantiation_token").data());
        if (token != fmu_->model_description()->instantiation_token) {
            throw error(make_error_code(errc::bad_file),
                "The serialized state of subsimulator '" + instanceName_ +
                    "' was created with a different FMU");
        }
        if (!fmu_->model_description()->capabilities.can_export_state) {
            throw error(make_error_code(errc::unsupported_feature),
                instanceName_ +
                    ": FMU does not support state deserialization");
        }

        const auto& serializedState = std::get<std::vector<std::byte>>(
            exportedState.get_child("serialized_fmu_state").data());
        parse_state_lifecycle(exportedState, schemeVersion, state);
        parse_state_clocks(exportedState, schemeVersion, state);
        if (schemeVersion >= 3) {
            parse_state_dimensions(exportedState, state);
        }
        const auto status = fmi3_import_de_serialize_fmu_state(
            handle_, reinterpret_cast<const fmi3_byte_t*>(serializedState.data()), // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): FMI reads the serialized byte buffer.
            serializedState.size(), &state.fmuState);
        check_status(callbackContext_.get(), "DeserializeFMUState", status);
    } catch (const boost::property_tree::ptree_bad_path&) {
        throw corrupt();
    } catch (const boost::property_tree::ptree_bad_data&) {
        throw corrupt();
    } catch (const std::bad_variant_access&) {
        throw corrupt();
    }
    const auto stateIndex = store_new_state(std::move(state));
    stateStored = true;
    return stateIndex;
}

void slave_instance::copy_current_state(saved_state& state)
{
    // The FMI state handle is paired with libcosim's lifecycle, time, and
    // Clock bookkeeping; restoring only the FMU handle would desynchronize
    // subsequent mode and routing checks.
    check_callback_reentrancy(callbackContext_.get(), instanceName_,
        "GetFMUState");
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "GetFMUState",
        operation_access::diagnostic);
    if (!fmu_->model_description()->capabilities.can_save_state) {
        throw error(make_error_code(errc::unsupported_feature),
            instanceName_ + ": FMU does not support state saving");
    }
    saved_state snapshot;
    const bool terminated =
        state_ == state::terminated || callbackContext_->error_terminated();
    snapshot.setupComplete = setupComplete_;
    snapshot.simStarted = !terminated && simStarted_;
    snapshot.lifecycleState = terminated ? state::terminated : state_;
    snapshot.stateBeforeConfiguration = stateBeforeConfiguration_;
    snapshot.currentTime = currentTime_;
    snapshot.stopTime = stopTime_;
    snapshot.terminationRequested = !terminated && terminationRequested_;
    snapshot.eventHandlingRequested = !terminated && eventHandlingRequested_;
    snapshot.stepSetStarted = !terminated && stepSetStarted_;
    if (!terminated) {
        snapshot.nextEventTime = nextEventTime_;
        snapshot.eventIterationComplete = eventIterationComplete_;
        snapshot.activeClocks = activeClocks_;
        snapshot.activatedPeriodicClocks = activatedPeriodicClocks_;
        snapshot.changingClocksAwaitingInterval = changingClocksAwaitingInterval_;
        snapshot.resolvedInputClockSchedules = resolvedInputClockSchedules_;
        snapshot.initialClockIntervals = initialClockIntervals_;
        snapshot.pendingClockIntervals = pendingClockIntervals_;
    }
    snapshot.configuredFixedClockSchedules = configuredFixedClockSchedules_;
    snapshot.hasDimensionValues = true;
    snapshot.dimensionValues = structuralParameterValues_;
    const auto status = fmi3_import_get_fmu_state(handle_, &state.fmuState);
    check_status(callbackContext_.get(), "GetFMUState", status);
    snapshot.fmuState = state.fmuState;
    static_assert(std::is_nothrow_move_assignable_v<saved_state>);
    state = std::move(snapshot);
}

slave::state_index slave_instance::store_new_state(saved_state state)
{
    if (savedStatesFreelist_.empty()) {
        savedStates_.push_back(std::move(state));
        return static_cast<state_index>(savedStates_.size() - 1);
    }

    const auto stateIndex = savedStatesFreelist_.front();
    savedStatesFreelist_.pop();
    savedStates_.at(stateIndex) = std::move(state);
    return stateIndex;
}

} // namespace cosim::fmi::v3
