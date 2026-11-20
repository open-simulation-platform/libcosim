/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "fmu_internal.hpp"

#include "cosim/error.hpp"
#include "cosim/exception.hpp"
#include "cosim/fmi/v3/fmu.hpp"
#include "cosim/model_description.hpp"
#include "cosim/slave.hpp"
#include "cosim/time.hpp"

#include <boost/range/algorithm/find.hpp>
#include <boost/range/algorithm/find_if.hpp>
#include <gsl/span>
#include <gsl/span_ext>

#include <FMI3/fmi3PlatformTypes.h>
#include <FMI3/fmi3_function_types.h>
#include <FMI3/fmi3_import_capi.h>
#include <FMI3/fmi3_types.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cosim::fmi::v3
{

using adapter_detail::check_available;
using adapter_detail::check_callback_reentrancy;
using adapter_detail::check_lifecycle;
using adapter_detail::check_status;

namespace
{

bool is_periodic_clock(clock_interval_variability variability)
{
    return variability == clock_interval_variability::constant ||
        variability == clock_interval_variability::fixed ||
        variability == clock_interval_variability::tunable;
}

struct decimal_schedule_values
{
    double interval;
    double shift;
};

struct fractional_schedule_values
{
    std::uint64_t interval;
    std::uint64_t shift;
    std::uint64_t resolution;
};

void use_decimal_schedule(cosim::clock_description& schedule, decimal_schedule_values values)
{
    schedule.interval_decimal = values.interval;
    schedule.shift_decimal = values.shift;
    schedule.interval_counter.reset();
    schedule.shift_counter.reset();
    schedule.resolution.reset();
}

void use_fractional_schedule(cosim::clock_description& schedule, fractional_schedule_values values)
{
    schedule.interval_decimal =
        static_cast<double>(values.interval) / static_cast<double>(values.resolution);
    schedule.shift_decimal.reset();
    schedule.interval_counter = values.interval;
    schedule.shift_counter = values.shift;
    schedule.resolution = values.resolution;
}

void set_fractional_schedule(cosim::clock_description& schedule,
    std::uint64_t interval,
    std::uint64_t intervalResolution,
    std::uint64_t shift, std::uint64_t shiftResolution,
    const std::string& instanceName)
{
    const auto intervalDivisor = std::gcd(interval, intervalResolution);
    interval /= intervalDivisor;
    intervalResolution /= intervalDivisor;
    const auto shiftDivisor = std::gcd(shift, shiftResolution);
    shift /= shiftDivisor;
    shiftResolution /= shiftDivisor;
    const auto divisor = std::gcd(intervalResolution, shiftResolution);
    const auto intervalMultiplier = shiftResolution / divisor;
    const auto shiftMultiplier = intervalResolution / divisor;
    // Parentheses protect max from the Windows macro.
    // NOLINTNEXTLINE(readability-redundant-parentheses)
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    if (intervalMultiplier == 0 || shiftMultiplier == 0 ||
        intervalResolution > maximum / intervalMultiplier ||
        interval > maximum / intervalMultiplier ||
        shift > maximum / shiftMultiplier) {
        throw error(make_error_code(errc::unsupported_feature),
            instanceName + ": Clock fraction cannot be represented safely");
    }
    use_fractional_schedule(schedule, {
                                          interval * intervalMultiplier,
                                          shift * shiftMultiplier,
                                          intervalResolution * intervalMultiplier,
                                      });
}

} // namespace

void slave_instance::resolve_fixed_clock_schedules()
{
    for (const auto& clock : fmu_->clock_descriptions()) {
        if (clock.causality != variable_causality::input ||
            (clock.interval_variability != clock_interval_variability::fixed &&
                clock.interval_variability != clock_interval_variability::tunable)) {
            continue;
        }
        resolve_fixed_clock_schedule(clock);
    }
    for (const auto& clock : fmu_->clock_descriptions()) {
        if (clock.causality != variable_causality::input ||
            (clock.interval_variability != clock_interval_variability::fixed &&
                clock.interval_variability != clock_interval_variability::tunable)) {
            continue;
        }
        apply_fixed_clock_schedule(clock);
    }
}

void slave_instance::resolve_fixed_clock_schedule(const clock_description& clock)
{
    const auto configured =
        configuredFixedClockSchedules_.find(clock.reference);
    if (configured != configuredFixedClockSchedules_.end()) {
        resolvedInputClockSchedules_.emplace(clock.reference, configured->second);
        return;
    }
    const auto* variable =
        fmu_->find_variable(variable_type::clock, clock.reference);
    if ((variable == nullptr) || !variable->clock) {
        callbackContext_->require_recovery();
        throw error(make_error_code(errc::model_error),
            instanceName_ + ": fixed Clock metadata is missing");
    }
    auto schedule = *variable->clock;
    if (clock.interval_decimal) {
        if (clock.interval_variability == clock_interval_variability::tunable) {
            if (schedule.interval_counter && schedule.resolution) {
                if (!schedule.shift_counter && schedule.shift_decimal &&
                    *schedule.shift_decimal != 0.0) {
                    throw error(make_error_code(errc::unsupported_feature),
                        instanceName_ +
                            ": tunable Clock has incompatible fractional "
                            "interval and decimal shift");
                }
                schedule.shift_counter = schedule.shift_counter.value_or(0);
                schedule.shift_decimal.reset();
            } else {
                schedule.shift_decimal = schedule.shift_decimal.value_or(0.0);
            }
            resolvedInputClockSchedules_.emplace(clock.reference, schedule);
        }
        return;
    }
    if (clock.supports_fraction) {
        resolve_fractional_clock_schedule(clock, schedule);
    } else {
        resolve_decimal_clock_schedule(clock, schedule);
    }
    resolvedInputClockSchedules_.emplace(clock.reference, schedule);
}

void slave_instance::resolve_fractional_clock_schedule(
    const clock_description& clock, cosim::clock_description& schedule)
{
    fmi3_interval_qualifier_t qualifier = fmi3_interval_not_yet_known;
    fmi3_uint64_t interval = 0;
    fmi3_uint64_t intervalResolution = 0;
    auto status = fmi3_import_get_interval_fraction(
        handle_, &clock.reference, 1, &interval, &intervalResolution,
        &qualifier);
    check_status(callbackContext_.get(), "GetIntervalFraction", status);
    if (qualifier == fmi3_interval_unchanged) {
        const auto cached = initialClockIntervals_.find(clock.reference);
        if (cached != initialClockIntervals_.end()) {
            const auto& cachedInterval = cached->second;
            if (cachedInterval.counter) {
                interval = *cachedInterval.counter;
                // Cached fractional intervals always include a resolution.
                // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
                intervalResolution = *cachedInterval.resolution;
                qualifier = fmi3_interval_changed;
            }
        }
    }
    fmi3_uint64_t shift = 0;
    fmi3_uint64_t shiftResolution = 0;
    status = fmi3_import_get_shift_fraction(handle_, &clock.reference, 1,
        &shift, &shiftResolution);
    check_status(callbackContext_.get(), "GetShiftFraction", status);
    if (qualifier != fmi3_interval_changed || interval == 0 ||
        intervalResolution == 0 || shiftResolution == 0) {
        callbackContext_->require_recovery();
        throw error(
            make_error_code(errc::model_error),
            instanceName_ +
                ": fixed Clock has no valid initial fractional schedule");
    }
    initialClockIntervals_.insert_or_assign(
        clock.reference, clock_interval_update{
                             clock_interval_qualifier::changed,
                             static_cast<double>(interval) / static_cast<double>(intervalResolution),
                             interval,
                             intervalResolution,
                         });
    set_fractional_schedule(schedule, interval, intervalResolution, shift,
        shiftResolution, instanceName_);
}

void slave_instance::resolve_decimal_clock_schedule(
    const clock_description& clock, cosim::clock_description& schedule)
{
    fmi3_interval_qualifier_t qualifier = fmi3_interval_not_yet_known;
    fmi3_float64_t interval = 0.0;
    auto status = fmi3_import_get_interval_decimal(handle_, &clock.reference,
        1, &interval, &qualifier);
    check_status(callbackContext_.get(), "GetIntervalDecimal", status);
    if (qualifier == fmi3_interval_unchanged) {
        const auto cached = initialClockIntervals_.find(clock.reference);
        if (cached != initialClockIntervals_.end() && !cached->second.counter) {
            interval = cached->second.interval;
            qualifier = fmi3_interval_changed;
        }
    }
    fmi3_float64_t shift = 0.0;
    status =
        fmi3_import_get_shift_decimal(handle_, &clock.reference, 1, &shift);
    check_status(callbackContext_.get(), "GetShiftDecimal", status);
    if (qualifier != fmi3_interval_changed || !std::isfinite(interval) ||
        interval <= 0.0 || !std::isfinite(shift) || shift < 0.0) {
        callbackContext_->require_recovery();
        throw error(make_error_code(errc::model_error),
            instanceName_ +
                ": fixed Clock has no valid initial decimal schedule");
    }
    schedule.interval_decimal = interval;
    schedule.shift_decimal = shift;
    initialClockIntervals_.insert_or_assign(
        clock.reference,
        clock_interval_update{clock_interval_qualifier::changed, interval, {}, {}});
}

void slave_instance::apply_fixed_clock_schedule(const clock_description& clock)
{
    const auto* variable =
        fmu_->find_variable(variable_type::clock, clock.reference);
    if ((variable == nullptr) || !variable->clock) {
        callbackContext_->require_recovery();
        throw error(make_error_code(errc::model_error),
            instanceName_ + ": fixed Clock metadata is missing");
    }
    const auto resolved = resolvedInputClockSchedules_.find(clock.reference);
    const auto& schedule = resolved != resolvedInputClockSchedules_.end()
        ? resolved->second
        : *variable->clock;
    if (!clock.interval_decimal) {
        return;
    }
    if (configuredFixedClockSchedules_.count(clock.reference) != 0) {
        if (schedule.interval_counter && schedule.resolution) {
            // Configured fractional schedules always include their shift.
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            const auto& shift = schedule.shift_counter.value();
            const auto status = fmi3_import_set_shift_fraction(
                handle_, &clock.reference, 1, &shift,
                &*schedule.resolution);
            check_status(callbackContext_.get(), "SetShiftFraction", status);
        } else {
            // Configured decimal schedules always include their shift.
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            const auto& shift = schedule.shift_decimal.value();
            const auto status = fmi3_import_set_shift_decimal(
                handle_, &clock.reference, 1, &shift);
            check_status(callbackContext_.get(), "SetShiftDecimal", status);
        }
    }
    if (schedule.supports_fraction && schedule.interval_counter &&
        schedule.resolution) {
        if (*schedule.interval_counter == 0 || *schedule.resolution == 0) {
            callbackContext_->require_recovery();
            throw error(make_error_code(errc::unsupported_feature),
                instanceName_ +
                    ": fixed Clock interval fraction is invalid");
        }
        const auto status = fmi3_import_set_interval_fraction(
            handle_, &clock.reference, 1, &*schedule.interval_counter,
            &*schedule.resolution);
        check_status(callbackContext_.get(), "SetIntervalFraction", status);
    } else {
        if (!schedule.interval_decimal ||
            !std::isfinite(*schedule.interval_decimal) ||
            *schedule.interval_decimal <= 0.0) {
            callbackContext_->require_recovery();
            throw error(make_error_code(errc::model_error),
                instanceName_ + ": fixed Clock interval is invalid");
        }
        const auto status = fmi3_import_set_interval_decimal(
            handle_, &clock.reference, 1, &*schedule.interval_decimal);
        check_status(callbackContext_.get(), "SetIntervalDecimal", status);
    }
}

clock_interval_update
// NOLINTNEXTLINE(readability-function-cognitive-complexity): Keep interval validation and the FMI query together.
slave_instance::get_input_clock_interval(value_reference reference)
{
    check_callback_reentrancy(callbackContext_.get(), instanceName_,
        "GetClockInterval");
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "GetClockInterval");
    check_lifecycle(state_ == state::initialization || state_ == state::event,
        instanceName_, "GetClockInterval");
    const auto* variable = fmu_->find_variable(variable_type::clock, reference);
    if ((variable == nullptr) || !variable->clock ||
        variable->causality != variable_causality::input ||
        (variable->clock->interval_variability !=
                cosim::clock_interval_variability::changing &&
            variable->clock->interval_variability !=
                cosim::clock_interval_variability::countdown &&
            (variable->clock->interval_variability !=
                    cosim::clock_interval_variability::tunable ||
                variable->clock->interval_decimal))) {
        throw error(make_error_code(errc::invalid_system_structure),
            instanceName_ + ": Clock " + std::to_string(reference) +
                " does not have an FMU-controlled interval");
    }
    const bool changingInEventMode =
        state_ == state::event && variable->clock->interval_variability == cosim::clock_interval_variability::changing;
    if (changingInEventMode &&
        changingClocksAwaitingInterval_.count(reference) == 0) {
        throw error(make_error_code(errc::model_error),
            instanceName_ + ": interval queries in Event Mode require an "
                            "activation of the changing Clock");
    }

    fmi3_interval_qualifier_t qualifier = fmi3_interval_not_yet_known;
    clock_interval_update result{clock_interval_qualifier::not_yet_known, 0.0, {}, {}};
    if (variable->clock->supports_fraction) {
        fmi3_uint64_t counter = 0;
        fmi3_uint64_t resolution = 0;
        const auto status = fmi3_import_get_interval_fraction(
            handle_, &reference, 1, &counter, &resolution, &qualifier);
        check_status(callbackContext_.get(), "GetIntervalFraction", status);
        if (qualifier == fmi3_interval_changed) {
            if (resolution == 0) {
                callbackContext_->require_recovery();
                throw error(make_error_code(errc::model_error),
                    instanceName_ +
                        ": changing Clock returned an invalid resolution");
            }
            result.counter = counter;
            result.resolution = resolution;
            result.interval =
                static_cast<double>(counter) / static_cast<double>(resolution);
        }
    } else {
        fmi3_float64_t interval = 0.0;
        const auto status = fmi3_import_get_interval_decimal(handle_, &reference, 1,
            &interval, &qualifier);
        check_status(callbackContext_.get(), "GetIntervalDecimal", status);
        if (qualifier == fmi3_interval_changed) {
            if (!std::isfinite(interval) || interval < 0.0) {
                callbackContext_->require_recovery();
                throw error(make_error_code(errc::model_error),
                    instanceName_ +
                        ": changing Clock returned an invalid interval");
            }
            result.interval = interval;
        }
    }
    switch (qualifier) {
        case fmi3_interval_changed:
            result.qualifier = clock_interval_qualifier::changed;
            if (state_ == state::initialization) {
                initialClockIntervals_.insert_or_assign(reference, result);
            }
            break;
        case fmi3_interval_unchanged:
            result.qualifier = clock_interval_qualifier::unchanged;
            break;
        case fmi3_interval_not_yet_known:
            break;
        default:
            callbackContext_->require_recovery();
            throw error(make_error_code(errc::model_error),
                instanceName_ +
                    ": changing Clock returned an invalid interval qualifier");
    }
    if (changingInEventMode) {
        if (result.qualifier == clock_interval_qualifier::not_yet_known) {
            callbackContext_->require_recovery();
            throw error(make_error_code(errc::model_error),
                instanceName_ + ": changing Clock returned no next interval");
        }
        changingClocksAwaitingInterval_.erase(reference);
    }
    return result;
}

void slave_instance::resolve_changing_clock_schedules()
{
    for (const auto& clock : fmu_->clock_descriptions()) {
        if (clock.causality != variable_causality::input ||
            (clock.interval_variability != clock_interval_variability::changing &&
                clock.interval_variability != clock_interval_variability::countdown)) {
            continue;
        }
        auto interval = get_input_clock_interval(clock.reference);
        if (interval.qualifier == clock_interval_qualifier::unchanged) {
            const auto cached = initialClockIntervals_.find(clock.reference);
            if (cached != initialClockIntervals_.end()) {
                interval = cached->second;
            }
        }
        if (clock.interval_variability == clock_interval_variability::countdown &&
            interval.qualifier == clock_interval_qualifier::not_yet_known) {
            continue;
        }
        if (interval.qualifier != clock_interval_qualifier::changed) {
            callbackContext_->require_recovery();
            throw error(make_error_code(errc::model_error),
                instanceName_ + ": changing Clock has no initial interval");
        }
        const auto* variable =
            fmu_->find_variable(variable_type::clock, clock.reference);
        if ((variable == nullptr) || !variable->clock) {
            callbackContext_->require_recovery();
            throw error(make_error_code(errc::model_error),
                instanceName_ + ": changing Clock metadata is missing");
        }
        auto schedule = *variable->clock;
        schedule.interval_decimal = interval.interval;
        schedule.interval_counter = interval.counter;
        schedule.resolution = interval.resolution;
        if (interval.counter) {
            schedule.shift_counter = 0;
            schedule.shift_decimal.reset();
        } else {
            schedule.shift_counter.reset();
            schedule.shift_decimal = 0.0;
        }
        resolvedInputClockSchedules_.emplace(clock.reference, schedule);
    }
}

std::optional<cosim::clock_description>
slave_instance::resolved_input_clock_schedule(value_reference reference) const
{
    check_callback_reentrancy(callbackContext_.get(), instanceName_,
        "ResolvedInputClockSchedule");
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_,
        "ResolvedInputClockSchedule");
    const auto schedule = resolvedInputClockSchedules_.find(reference);
    if (schedule == resolvedInputClockSchedules_.end()) {
        return std::nullopt;
    }
    return schedule->second;
}

cosim::clock_description
slave_instance::fixed_clock_for_configuration(value_reference reference,
    const char* operation) const
{
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, operation);
    check_lifecycle(state_ == state::instantiated ||
            state_ == state::initialization,
        instanceName_, operation);
    const auto* variable = fmu_->find_variable(variable_type::clock, reference);
    if ((variable == nullptr) || !variable->clock ||
        variable->causality != variable_causality::input ||
        variable->clock->interval_variability !=
            cosim::clock_interval_variability::fixed ||
        !variable->clock->interval_decimal) {
        throw error(make_error_code(errc::invalid_system_structure),
            instanceName_ + ": Clock " + std::to_string(reference) +
                " is not an importer-configurable fixed input Clock");
    }
    return *variable->clock;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): Preserve the public Clock configuration API.
void slave_instance::configure_fixed_clock_decimal(value_reference reference,
    double interval,
    double shift)
{
    check_callback_reentrancy(callbackContext_.get(), instanceName_,
        "ConfigureFixedClockDecimal");
    std::scoped_lock const lock(mutex_);
    auto schedule =
        fixed_clock_for_configuration(reference, "ConfigureFixedClockDecimal");
    if (!std::isfinite(interval) || interval <= 0.0 || !std::isfinite(shift) ||
        shift < 0.0) {
        throw std::invalid_argument("Fixed Clock interval and shift must be finite "
                                    "and nonnegative, with a positive interval");
    }
    use_decimal_schedule(schedule, {interval, shift});
    configuredFixedClockSchedules_[reference] = schedule;
}

void slave_instance::configure_fixed_clock_fraction(
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): Preserve the public Clock configuration API.
    value_reference reference, std::uint64_t intervalCounter,
    std::uint64_t shiftCounter, std::uint64_t resolution)
{
    check_callback_reentrancy(callbackContext_.get(), instanceName_,
        "ConfigureFixedClockFraction");
    std::scoped_lock const lock(mutex_);
    auto schedule =
        fixed_clock_for_configuration(reference, "ConfigureFixedClockFraction");
    if (!schedule.supports_fraction) {
        throw error(make_error_code(errc::unsupported_feature),
            instanceName_ +
                ": fixed Clock does not support fractional configuration");
    }
    if (intervalCounter == 0 || resolution == 0) {
        throw std::invalid_argument(
            "Fixed Clock fractional interval and resolution must be positive");
    }
    use_fractional_schedule(schedule, {intervalCounter, shiftCounter, resolution});
    configuredFixedClockSchedules_[reference] = schedule;
}

void slave_instance::clear_fixed_clock_configuration(
    value_reference reference)
{
    check_callback_reentrancy(callbackContext_.get(), instanceName_,
        "ClearFixedClockConfiguration");
    std::scoped_lock const lock(mutex_);
    fixed_clock_for_configuration(reference, "ClearFixedClockConfiguration");
    configuredFixedClockSchedules_.erase(reference);
}

cosim::clock_description
slave_instance::tunable_clock_for_configuration(value_reference reference,
    const char* operation) const
{
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, operation);
    check_lifecycle(state_ == state::instantiated ||
            state_ == state::initialization,
        instanceName_, operation);
    const auto* variable = fmu_->find_variable(variable_type::clock, reference);
    if ((variable == nullptr) || !variable->clock ||
        variable->causality != variable_causality::input ||
        variable->clock->interval_variability !=
            cosim::clock_interval_variability::tunable ||
        !variable->clock->interval_decimal) {
        throw error(make_error_code(errc::invalid_system_structure),
            instanceName_ + ": Clock " + std::to_string(reference) +
                " is not a host-controlled tunable input Clock");
    }
    return *variable->clock;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): Preserve the public Clock configuration API.
void slave_instance::configure_tunable_clock_decimal(value_reference reference,
    double interval,
    double shift)
{
    check_callback_reentrancy(callbackContext_.get(), instanceName_,
        "ConfigureTunableClockDecimal");
    std::scoped_lock const lock(mutex_);
    auto schedule = tunable_clock_for_configuration(
        reference, "ConfigureTunableClockDecimal");
    if (!std::isfinite(interval) ||
        static_cast<long double>(interval) * duration::period::den < 1.0L ||
        !std::isfinite(shift) || shift < 0.0) {
        throw std::invalid_argument(
            "Tunable Clock interval must be at least one nanosecond and shift must "
            "be finite and nonnegative");
    }
    use_decimal_schedule(schedule, {interval, shift});
    configuredFixedClockSchedules_[reference] = schedule;
}

void slave_instance::configure_tunable_clock_fraction(
    // NOLINTNEXTLINE(bugprone-easily-swappable-parameters): Preserve the public Clock configuration API.
    value_reference reference, std::uint64_t intervalCounter,
    std::uint64_t shiftCounter, std::uint64_t resolution)
{
    check_callback_reentrancy(callbackContext_.get(), instanceName_,
        "ConfigureTunableClockFraction");
    std::scoped_lock const lock(mutex_);
    auto schedule = tunable_clock_for_configuration(
        reference, "ConfigureTunableClockFraction");
    if (!schedule.supports_fraction) {
        throw error(make_error_code(errc::unsupported_feature),
            instanceName_ + ": tunable Clock does not support fractions");
    }
    if (resolution == 0 ||
        intervalCounter < ((resolution - 1) / duration::period::den) + 1) {
        throw std::invalid_argument(
            "Tunable Clock fractional interval must be at least one nanosecond");
    }
    use_fractional_schedule(schedule, {intervalCounter, shiftCounter, resolution});
    configuredFixedClockSchedules_[reference] = schedule;
}

void slave_instance::clear_tunable_clock_configuration(
    value_reference reference)
{
    check_callback_reentrancy(callbackContext_.get(), instanceName_,
        "ClearTunableClockConfiguration");
    std::scoped_lock const lock(mutex_);
    tunable_clock_for_configuration(reference, "ClearTunableClockConfiguration");
    configuredFixedClockSchedules_.erase(reference);
}

cosim::clock_description
slave_instance::tunable_clock_for_update(value_reference reference,
    const char* operation) const
{
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, operation);
    check_lifecycle(state_ == state::step || state_ == state::event,
        instanceName_, operation);
    const auto* variable = fmu_->find_variable(variable_type::clock, reference);
    const auto schedule = resolvedInputClockSchedules_.find(reference);
    if ((variable == nullptr) || !variable->clock ||
        variable->causality != variable_causality::input ||
        variable->clock->interval_variability !=
            cosim::clock_interval_variability::tunable ||
        !variable->clock->interval_decimal ||
        schedule == resolvedInputClockSchedules_.end()) {
        throw error(make_error_code(errc::invalid_system_structure),
            instanceName_ + ": Clock " + std::to_string(reference) +
                " is not a host-controlled tunable input Clock");
    }
    return schedule->second;
}

void slave_instance::set_tunable_clock_interval_decimal(
    value_reference reference, double interval)
{
    check_callback_reentrancy(callbackContext_.get(), instanceName_,
        "SetTunableClockIntervalDecimal");
    std::scoped_lock const lock(mutex_);
    const auto schedule =
        tunable_clock_for_update(reference, "SetTunableClockIntervalDecimal");
    if (!std::isfinite(interval) ||
        static_cast<long double>(interval) * duration::period::den < 1.0L) {
        throw std::invalid_argument(
            "Tunable Clock interval must be at least one nanosecond");
    }
    if (schedule.interval_counter) {
        throw error(
            make_error_code(errc::unsupported_feature),
            instanceName_ +
                ": tunable Clock interval representation cannot change at runtime");
    }
    pendingClockIntervals_[reference] = {
        clock_interval_qualifier::changed,
        interval,
        {},
        {},
    };
}

void slave_instance::set_tunable_clock_interval_fraction(
    value_reference reference, std::uint64_t counter,
    std::uint64_t resolution)
{
    check_callback_reentrancy(callbackContext_.get(), instanceName_,
        "SetTunableClockIntervalFraction");
    std::scoped_lock const lock(mutex_);
    auto schedule =
        tunable_clock_for_update(reference, "SetTunableClockIntervalFraction");
    if (resolution == 0 ||
        counter < ((resolution - 1) / duration::period::den) + 1) {
        throw std::invalid_argument(
            "Tunable Clock fractional interval must be at least one nanosecond");
    }
    if (!schedule.interval_counter || !schedule.supports_fraction) {
        throw error(
            make_error_code(errc::unsupported_feature),
            instanceName_ +
                ": tunable Clock interval representation cannot change at runtime");
    }
    // Resolved fractional schedules always contain both shift and resolution.
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    const auto shift = schedule.shift_counter.value();
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
    const auto shiftResolution = schedule.resolution.value();
    set_fractional_schedule(schedule, counter, resolution,
        shift, shiftResolution,
        instanceName_);
    pendingClockIntervals_[reference] = {
        clock_interval_qualifier::changed,
        static_cast<double>(counter) / static_cast<double>(resolution),
        counter,
        resolution,
    };
}

bool slave_instance::has_pending_clock_intervals() const
{
    check_callback_reentrancy(callbackContext_.get(), instanceName_,
        "HasPendingClockIntervals");
    std::scoped_lock const lock(mutex_);
    return !pendingClockIntervals_.empty();
}

std::optional<clock_interval_update>
slave_instance::pending_input_clock_interval(value_reference reference) const
{
    check_callback_reentrancy(callbackContext_.get(), instanceName_,
        "PendingInputClockInterval");
    std::scoped_lock const lock(mutex_);
    const auto pending = pendingClockIntervals_.find(reference);
    if (pending == pendingClockIntervals_.end()) {
        return std::nullopt;
    }
    return pending->second;
}

void slave_instance::apply_pending_clock_intervals()
{
    check_callback_reentrancy(callbackContext_.get(), instanceName_,
        "ApplyTunableClockIntervals");
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_,
        "ApplyTunableClockIntervals");
    check_lifecycle(state_ == state::event, instanceName_,
        "ApplyTunableClockIntervals");
    while (!pendingClockIntervals_.empty()) {
        const auto pending = pendingClockIntervals_.begin();
        const auto reference = pending->first;
        const auto interval = pending->second;
        auto schedule = resolvedInputClockSchedules_.at(reference);
        if (interval.counter) {
            // Resolved fractional schedules include shift and resolution.
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            const auto shift = schedule.shift_counter.value();
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            const auto shiftResolution = schedule.resolution.value();
            // Pending fractional intervals always include their resolution.
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            const auto& intervalResolution = interval.resolution.value();
            set_fractional_schedule(schedule, *interval.counter, intervalResolution,
                shift, shiftResolution, instanceName_);
            const auto status = fmi3_import_set_interval_fraction(
                handle_, &reference, 1, &*interval.counter, &intervalResolution);
            check_status(callbackContext_.get(), "SetIntervalFraction", status);
        } else {
            const auto status = fmi3_import_set_interval_decimal(
                handle_, &reference, 1, &interval.interval);
            check_status(callbackContext_.get(), "SetIntervalDecimal", status);
            schedule.interval_decimal = interval.interval;
        }
        resolvedInputClockSchedules_.at(reference) = schedule;
        pendingClockIntervals_.erase(pending);
    }
}

const std::vector<clock_description>&
slave_instance::clock_descriptions() const
{
    return fmu_->clock_descriptions();
}

cosim::clock_controller* slave_instance::get_clock_controller() noexcept
{
    return eventModeUsed_ && !fmu_->clock_descriptions().empty() ? this : nullptr;
}

cosim::event_controller* slave_instance::get_event_controller() noexcept
{
    return eventModeUsed_ ? this : nullptr;
}

bool slave_instance::is_in_event_mode() const noexcept
{
    return state_ == state::event;
}

bool slave_instance::get_output_clock(value_reference reference)
{
    bool active = false;
    get_clocks(gsl::make_span(&reference, 1), gsl::make_span(&active, 1));
    return active;
}

void slave_instance::set_input_clock(value_reference reference, bool active)
{
    set_clocks(gsl::make_span(&reference, 1), gsl::make_span(&active, 1));
}

variable_value slave_instance::get_clocked_variable(variable_type type,
    value_reference reference)
{
    const typed_value_reference variable{type, reference};
    auto values = get_variable_values(gsl::make_span(&variable, 1));
    assert(values.size() == 1);
    return std::move(values.front());
}

void slave_instance::set_clocked_variable(variable_type type,
    value_reference reference,
    const variable_value& value)
{
    const typed_value_reference variable{type, reference};
    set_variable_values(gsl::make_span(&variable, 1), gsl::make_span(&value, 1));
}

clock_update_result slave_instance::update_clock_state()
{
    const auto update = update_discrete_states();
    return {
        update.discrete_states_need_update,
        update.terminate_simulation,
        update.next_event_time,
    };
}

void slave_instance::get_clocks(gsl::span<const value_reference> clocks,
    gsl::span<bool> values) const
{
    check_callback_reentrancy(callbackContext_.get(), instanceName_, "GetClock");
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "GetClock");
    check_lifecycle(state_ == state::event, instanceName_, "GetClock");
    COSIM_INPUT_CHECK(clocks.size() == values.size());
    if (clocks.empty()) {
        return;
    }

    for (const auto reference : clocks) {
        const auto* variable = fmu_->find_variable(variable_type::clock, reference);
        const auto clock =
            std::find_if(clock_descriptions().begin(), clock_descriptions().end(),
                [reference](const auto& candidate) {
                    return candidate.reference == reference;
                });
        if (variable == nullptr || clock == clock_descriptions().end() ||
            (clock->causality != variable_causality::output &&
                clock->causality != variable_causality::local) ||
            clock->interval_variability != clock_interval_variability::triggered) {
            throw error(make_error_code(errc::invalid_system_structure),
                instanceName_ + ": value reference " +
                    std::to_string(reference) +
                    " is not a triggered output or local Clock");
        }
        check_clocked_variable_access(*variable, "GetClock");
    }

    // FMI requires a contiguous Clock buffer; vector<bool> has no such storage.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    const auto fmiValues = std::make_unique<fmi3_clock_t[]>(clocks.size());
    const auto status = fmi3_import_get_clock(handle_, clocks.data(),
        clocks.size(), fmiValues.get());
    check_status(callbackContext_.get(), "GetClock", status);
    const auto fmiSpan = gsl::make_span(fmiValues.get(), clocks.size());
    auto fmiValue = fmiSpan.begin();
    auto reference = clocks.begin();
    for (auto& value : values) {
        value = static_cast<int>(*fmiValue++) != fmi3_false;
        if (value) {
            activeClocks_.insert(*reference);
        } else {
            activeClocks_.erase(*reference);
        }
        ++reference;
    }
}

void slave_instance::set_clocks(gsl::span<const value_reference> clocks,
    gsl::span<const bool> values)
{
    check_callback_reentrancy(callbackContext_.get(), instanceName_, "SetClock");
    std::scoped_lock const lock(mutex_);
    callbackContext_->rethrow_callback_exception();
    check_available(callbackContext_.get(), instanceName_, "SetClock");
    check_lifecycle(state_ == state::event, instanceName_, "SetClock");
    COSIM_INPUT_CHECK(clocks.size() == values.size());
    if (clocks.empty()) {
        return;
    }

    auto requestedPeriodicClocks = activatedPeriodicClocks_;
    auto value = values.begin();
    for (const auto reference : clocks) {
        const auto* variable = fmu_->find_variable(variable_type::clock, reference);
        const auto clock =
            std::find_if(clock_descriptions().begin(), clock_descriptions().end(),
                [reference](const auto& candidate) {
                    return candidate.reference == reference;
                });
        if (variable == nullptr || clock == clock_descriptions().end() ||
            clock->causality != variable_causality::input) {
            throw error(make_error_code(errc::invalid_system_structure),
                instanceName_ + ": value reference " +
                    std::to_string(reference) + " is not an input Clock");
        }
        if (!*value && !clock->can_be_deactivated) {
            throw error(make_error_code(errc::unsupported_feature),
                instanceName_ + ": Clock " + std::to_string(reference) +
                    " cannot be deactivated");
        }
        if (*value &&
            is_periodic_clock(clock->interval_variability) &&
            !requestedPeriodicClocks.insert(reference).second) {
            throw error(make_error_code(errc::model_error),
                instanceName_ + ": periodic Clock " +
                    std::to_string(reference) +
                    " may only be activated once per Event Mode");
        }
        check_clocked_variable_access(*variable, "SetClock");
        ++value;
    }

    // FMI requires a contiguous Clock buffer; vector<bool> has no such storage.
    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    const auto fmiValues = std::make_unique<fmi3_clock_t[]>(clocks.size());
    const auto fmiSpan = gsl::make_span(fmiValues.get(), clocks.size());
    auto fmiValue = fmiSpan.begin();
    for (const auto active : values) {
        *fmiValue++ = ((active ? fmi3_true : fmi3_false) != 0);
    }
    const auto status = fmi3_import_set_clock(handle_, clocks.data(),
        clocks.size(), fmiValues.get());
    check_status(callbackContext_.get(), "SetClock", status);
    value = values.begin();
    for (const auto reference : clocks) {
        if (*value++) {
            activeClocks_.insert(reference);
            const auto clock =
                std::find_if(clock_descriptions().begin(), clock_descriptions().end(),
                    [reference](const auto& candidate) {
                        return candidate.reference == reference;
                    });
            if (is_periodic_clock(clock->interval_variability)) {
                activatedPeriodicClocks_.insert(reference);
            } else if (clock->interval_variability ==
                clock_interval_variability::changing) {
                changingClocksAwaitingInterval_.insert(reference);
            }
        } else {
            deactivate_clock_and_dependents(reference);
        }
    }
    eventIterationComplete_ = false;
}

void slave_instance::deactivate_clock_and_dependents(value_reference clock)
{
    std::vector<value_reference> pending{clock};
    while (!pending.empty()) {
        const auto deactivated = pending.back();
        pending.pop_back();
        activeClocks_.erase(deactivated);
        for (const auto& variable : fmu_->model_description()->variables) {
            if (variable.type != variable_type::clock ||
                activeClocks_.count(variable.reference) == 0 ||
                boost::range::find(variable.clocks, deactivated) ==
                    variable.clocks.end()) {
                continue;
            }
            pending.push_back(variable.reference);
        }
    }
}

void slave_instance::check_clocked_variable_access(
    const variable_description& variable, const char* operation) const
{
    // A clock-associated variable is accessible only while its owning Clock
    // is active in the current Event Mode instant.
    if (variable.clocks.empty() || state_ == state::initialization) {
        return;
    }
    if (state_ != state::event) {
        throw error(make_error_code(errc::model_error),
            instanceName_ + ": clocked variable " +
                std::to_string(variable.reference) +
                " may only be accessed in Initialization or Event Mode");
    }
    const auto activeClock =
        boost::range::find_if(variable.clocks, [this](value_reference reference) {
            return activeClocks_.count(reference) != 0;
        });
    if (activeClock == variable.clocks.end()) {
        throw error(make_error_code(errc::model_error),
            instanceName_ + ": " + operation +
                " requires an active Clock for variable " +
                std::to_string(variable.reference));
    }
}

} // namespace cosim::fmi::v3
