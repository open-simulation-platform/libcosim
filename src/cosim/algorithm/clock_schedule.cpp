/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "clock_router.hpp"

#include "cosim/exception.hpp"
#include "cosim/execution.hpp"
#include "cosim/model_description.hpp"
#include "cosim/slave.hpp"
#include "cosim/time.hpp"

#include <boost/multiprecision/cpp_int.hpp> // IWYU pragma: keep

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace cosim
{

namespace
{
constexpr int binary64_significand_bits = 53;
constexpr int clock_shift_tolerance_epsilons = 8;
} // namespace

void clock_router::initialize(time_point startTime)
{
    validate_clock_feedback();
    start_time_ = startTime;
    next_event_time_.reset();
    terminated_ = false;
    periodic_clocks_.clear();
    changing_clocks_.clear();

    for (const auto& entry : simulators_) {
        const auto& info = entry.second;
        for (const auto& variable : info.description->variables) {
            if (variable.type != variable_type::clock) {
                continue;
            }
            require_controller(entry.first);
            if (variable.causality == variable_causality::input && variable.clock &&
                (variable.clock->interval_variability == clock_interval_variability::changing ||
                    variable.clock->interval_variability == clock_interval_variability::countdown ||
                    variable.clock->interval_variability == clock_interval_variability::tunable)) {
                initialize_changing_clock(entry.first, info, variable, *variable.clock, startTime);
                continue;
            }
            if (variable.causality != variable_causality::input ||
                !variable.clock ||
                (variable.clock->interval_variability !=
                        clock_interval_variability::constant &&
                    variable.clock->interval_variability !=
                        clock_interval_variability::fixed)) {
                continue;
            }
            initialize_periodic_clock(entry.first, info, variable, startTime);
        }
    }

    if (process_legacy_events(startTime, {}) ||
        (has_clock_support() && process_event(startTime))) {
        throw error(
            make_error_code(errc::simulation_error),
            "A simulator requested termination during initialization");
    }
}

void clock_router::validate_clock_feedback() const
{
    for (const auto& data : clocked_data_connections_) {
        // Ordinary sources are sampled when the target Clock activates,
        // and ordinary targets receive values as clocked outputs are sampled.
        if (describe(data.source).clocks.empty() || describe(data.target).clocks.empty()) {
            continue;
        }
        if (clock_routed(data) && clock_feedback_path_exists(data)) {
            throw error(
                make_error_code(errc::unsupported_feature),
                "Clock-associated data feedback through a cyclic Clock network is not supported");
        }
    }
}

void clock_router::initialize_changing_clock(simulator_index index, const simulator_info& info,
    const variable_description& variable, const clock_description& description, time_point startTime)
{
    const bool countdown = description.interval_variability ==
        clock_interval_variability::countdown;
    const bool tunable = description.interval_variability ==
        clock_interval_variability::tunable;
    const auto resolved = info.controller->resolved_input_clock_schedule(variable.reference);
    if ((!resolved || !resolved->interval_decimal) && !countdown) {
        throw error(make_error_code(errc::model_error),
            "A changing input Clock has no initial interval");
    }
    clock_interval_update interval{
        clock_interval_qualifier::changed,
        resolved && resolved->interval_decimal ? *resolved->interval_decimal : 0.0,
        {},
        {},
    };
    if (resolved) {
        interval.counter = resolved->interval_counter;
        interval.resolution = resolved->resolution;
    } else if (description.supports_fraction) {
        interval.counter = 0;
        interval.resolution = 1;
    }
    changing_clock clock{
        variable_id{index, variable_type::clock, variable.reference},
        interval,
        startTime,
        std::nullopt,
        countdown,
        resolved && resolved->interval_decimal.has_value(),
        startTime.time_since_epoch().count(),
        1,
        tunable,
        tunable && !description.interval_decimal,
        false,
    };
    if (tunable && resolved) {
        validate_tunable_interval(clock.interval);
        auto shift = clock;
        shift.interval.interval = resolved->shift_decimal.value_or(0.0);
        if (clock.interval.counter) {
            shift.interval.counter = resolved->shift_counter.value_or(0);
            shift.interval.resolution = resolved->resolution;
            shift.interval.interval = static_cast<double>(*shift.interval.counter) /
                // NOLINTNEXTLINE(bugprone-unchecked-optional-access): Validation pairs the counter and resolution.
                static_cast<double>(shift.interval.resolution.value());
        }
        auto first = changing_exact_time(shift);
        clock.origin_numerator = std::move(first.first);
        clock.origin_denominator = std::move(first.second);
        clock.origin = rounded_clock_time(clock.origin_numerator,
            clock.origin_denominator, shift.interval.counter.has_value());
        clock.next = clock.origin;
    } else if (clock.known) {
        clock.next = changing_time(clock);
    }
    changing_clocks_.push_back(std::move(clock));
}

void clock_router::initialize_periodic_clock(simulator_index index, const simulator_info& info,
    const variable_description& variable, time_point startTime)
{
    auto schedule = variable.clock;
    const auto resolved = info.controller->resolved_input_clock_schedule(variable.reference);
    if (resolved) {
        schedule = resolved;
    }
    if (!schedule || !schedule->interval_decimal ||
        !std::isfinite(*schedule->interval_decimal) ||
        *schedule->interval_decimal <= 0.0) {
        throw error(
            make_error_code(errc::unsupported_feature),
            "Only positive declared FMI 3 Clock intervals are "
            "supported by time-stepped algorithms");
    }
    periodic_clock scheduled{
        variable_id{index, variable_type::clock, variable.reference},
        duration::zero(),
        startTime,
        std::nullopt,
        std::nullopt,
    };
    if (schedule->supports_fraction &&
        (schedule->interval_counter || schedule->resolution)) {
        initialize_fractional_schedule(scheduled, *schedule, startTime);
    } else {
        initialize_decimal_schedule(scheduled, *schedule, *schedule->interval_decimal, startTime);
    }
    periodic_clocks_.push_back(scheduled);
}

void clock_router::initialize_fractional_schedule(
    periodic_clock& scheduled, const clock_description& schedule, time_point startTime)
{
    if (!schedule.interval_counter || !schedule.resolution ||
        *schedule.interval_counter == 0 || *schedule.resolution == 0 ||
        boost::multiprecision::cpp_int(*schedule.interval_counter) *
                duration::period::den <
            *schedule.resolution) {
        throw error(
            make_error_code(errc::unsupported_feature),
            "The fractional FMI 3 Clock interval is invalid or shorter than one nanosecond");
    }
    const auto intervalCounter = schedule.interval_counter.value();
    // NOLINTNEXTLINE(bugprone-unchecked-optional-access): The guard above requires a resolution.
    const auto resolution = schedule.resolution.value();
    const auto shift = schedule.shift_counter.value_or(0);
    const auto decimalShift = schedule.shift_decimal.value_or(0.0);
    if (!std::isfinite(decimalShift) || decimalShift < 0.0) {
        throw error(make_error_code(errc::unsupported_feature),
            "The FMI 3 Clock shift is invalid");
    }
    if (shift != 0 && decimalShift != 0.0) {
        const auto fractionalShift = static_cast<double>(shift) / static_cast<double>(resolution);
        const auto scale = std::max(decimalShift, fractionalShift);
        if (std::abs(decimalShift - fractionalShift) >
            clock_shift_tolerance_epsilons * std::numeric_limits<double>::epsilon() * scale) {
            throw error(make_error_code(errc::unsupported_feature),
                "The decimal and fractional FMI 3 Clock shifts disagree");
        }
    }
    const auto& fraction = scheduled.fraction.emplace(fractional_schedule{
        startTime,
        intervalCounter,
        resolution,
        shift,
        0,
        shift == 0 && decimalShift != 0.0
            ? std::optional<double>(decimalShift)
            : std::nullopt,
    });
    scheduled.next = fractional_time(fraction);
}

void clock_router::initialize_decimal_schedule(
    periodic_clock& scheduled, const clock_description& schedule,
    double intervalSeconds, time_point startTime)
{
    const auto shift = schedule.shift_decimal.value_or(0.0);
    if (!std::isfinite(shift) || shift < 0.0) {
        throw error(make_error_code(errc::unsupported_feature),
            "The FMI 3 Clock shift is invalid");
    }
    const auto interval = binary_fraction(intervalSeconds);
    if (interval.first * duration::period::den < interval.second) {
        throw error(
            make_error_code(errc::unsupported_feature),
            "The FMI 3 Clock interval cannot be represented at the "
            "simulation time precision");
    }
    const auto& decimal = scheduled.decimal.emplace(decimal_schedule{
        startTime,
        intervalSeconds,
        shift,
        0,
        false,
    });
    scheduled.next = decimal_time(decimal);
    scheduled.interval = legacy_decimal_interval(decimal);
}

duration clock_router::cap_step(time_point currentTime, duration proposed) const
{
    check_recovery();
    auto result = proposed;
    if (next_event_time_ && *next_event_time_ > currentTime) {
        result = std::min(result, *next_event_time_ - currentTime);
    }
    for (const auto& entry : simulators_) {
        const auto next = entry.second.sim->get_event_handling_status().next_event_time;
        if (next && *next > currentTime) {
            result = std::min(result, *next - currentTime);
        }
    }
    return result;
}

void clock_router::validate_pending_intervals(time_point currentTime) const
{
    for (const auto& clock : changing_clocks_) {
        if (!clock.tunable || clock.fmu_controlled) {
            continue;
        }
        const auto update = require_controller(clock.id.simulator)
                                .pending_input_clock_interval(clock.id.reference);
        if (!update) {
            continue;
        }
        validate_tunable_interval(*update);
        auto revised = clock;
        revised.interval = *update;
        if (clock.ticked && changing_time(revised) < currentTime) {
            throw error(make_error_code(errc::unsupported_feature),
                "A tunable FMI 3 Clock interval schedules an unrepresentable or past tick");
        }
    }
}

std::pair<boost::multiprecision::cpp_int, boost::multiprecision::cpp_int>
clock_router::binary_fraction(double value)
{
    using boost::multiprecision::cpp_int;
    int exponent = 0;
    const auto mantissa = static_cast<std::uint64_t>(
        std::ldexp(std::frexp(value, &exponent), binary64_significand_bits));
    cpp_int numerator = mantissa;
    cpp_int denominator = 1;
    if (exponent >= binary64_significand_bits) {
        numerator <<= exponent - binary64_significand_bits;
    } else {
        denominator <<= binary64_significand_bits - exponent;
    }
    return {std::move(numerator), std::move(denominator)};
}

time_point clock_router::rounded_clock_time(
    const boost::multiprecision::cpp_int& numerator,
    const boost::multiprecision::cpp_int& denominator,
    bool fractional)
{
    using boost::multiprecision::cpp_int;
    // Decimal intervals carry binary floating-point representation error.
    // Preserve the no-early-tick rule for explicitly rational schedules.
    const cpp_int offset = fractional ? cpp_int(denominator - 1) : cpp_int(denominator / 2);
    const cpp_int ticks = numerator >= 0
        ? cpp_int((numerator + offset) / denominator)
        : cpp_int((numerator - (fractional ? cpp_int(0) : offset)) / denominator);
    if (ticks > std::numeric_limits<duration::rep>::max() ||
        ticks < std::numeric_limits<duration::rep>::min()) {
        throw error(make_error_code(errc::unsupported_feature),
            "The aperiodic FMI 3 Clock schedule exceeds the simulation time range");
    }
    return time_point(duration(ticks.convert_to<duration::rep>()));
}

std::pair<boost::multiprecision::cpp_int, boost::multiprecision::cpp_int>
clock_router::changing_exact_time(const changing_clock& clock)
{
    using boost::multiprecision::cpp_int;
    const auto& interval = clock.interval;
    if (interval.qualifier != clock_interval_qualifier::changed ||
        !std::isfinite(interval.interval) || interval.interval < 0.0 ||
        interval.counter.has_value() != interval.resolution.has_value()) {
        throw error(make_error_code(errc::model_error),
            "The changing FMI 3 Clock interval is invalid");
    }
    cpp_int numerator;
    cpp_int denominator;
    if (interval.counter) {
        if (*interval.resolution == 0 ||
            static_cast<double>(*interval.counter) /
                    static_cast<double>(*interval.resolution) !=
                interval.interval) {
            throw error(make_error_code(errc::model_error),
                "The changing FMI 3 Clock fraction is invalid");
        }
        numerator = *interval.counter;
        denominator = *interval.resolution;
    } else {
        auto fraction = binary_fraction(interval.interval);
        numerator = std::move(fraction.first);
        denominator = std::move(fraction.second);
    }
    numerator = clock.origin_numerator * denominator +
        numerator * duration::period::den * clock.origin_denominator;
    denominator *= clock.origin_denominator;
    cpp_int dividend = numerator < 0 ? cpp_int(-numerator) : numerator;
    cpp_int divisor = denominator;
    while (divisor != 0) {
        cpp_int remainder = dividend % divisor;
        dividend = divisor;
        divisor = remainder;
    }
    numerator /= dividend;
    denominator /= dividend;
    if (boost::multiprecision::msb(denominator) > max_clock_denominator_msb) { // NOLINT(misc-include-cleaner)
        throw error(make_error_code(errc::unsupported_feature),
            "The aperiodic FMI 3 Clock fraction exceeds the supported precision");
    }
    return {std::move(numerator), std::move(denominator)};
}

time_point clock_router::changing_time(const changing_clock& clock)
{
    const auto exact = changing_exact_time(clock);
    return rounded_clock_time(exact.first, exact.second, clock.interval.counter.has_value());
}

void clock_router::validate_tunable_interval(const clock_interval_update& interval)
{
    using boost::multiprecision::cpp_int;
    if (interval.qualifier != clock_interval_qualifier::changed ||
        !std::isfinite(interval.interval) || interval.interval <= 0.0 ||
        interval.counter.has_value() != interval.resolution.has_value()) {
        throw error(make_error_code(errc::model_error),
            "The tunable FMI 3 Clock interval is invalid");
    }
    if (interval.counter) {
        if (*interval.resolution == 0 || *interval.counter == 0 ||
            cpp_int(*interval.counter) * duration::period::den < *interval.resolution) {
            throw error(make_error_code(errc::unsupported_feature),
                "The tunable FMI 3 Clock interval is shorter than one nanosecond");
        }
        if (static_cast<double>(*interval.counter) /
                static_cast<double>(*interval.resolution) !=
            interval.interval) {
            throw error(make_error_code(errc::model_error),
                "The tunable FMI 3 Clock fraction is inconsistent");
        }
    } else {
        const auto fraction = binary_fraction(interval.interval);
        if (fraction.first * duration::period::den < fraction.second) {
            throw error(make_error_code(errc::unsupported_feature),
                "The tunable FMI 3 Clock interval is shorter than one nanosecond");
        }
    }
}

void clock_router::activate_changing_clock(changing_clock& clock, time_point currentTime)
{
    if (clock.tunable && !clock.ticked && clock.next && *clock.next == currentTime) {
        clock.ticked = true;
    } else if (clock.next && *clock.next == currentTime) {
        auto exact = changing_exact_time(clock);
        clock.origin_numerator = std::move(exact.first);
        clock.origin_denominator = std::move(exact.second);
    } else {
        clock.origin_numerator = currentTime.time_since_epoch().count();
        clock.origin_denominator = 1;
    }
    clock.origin = currentTime;
    if (clock.tunable) {
        clock.ticked = true;
    }
}

time_point clock_router::decimal_time(const decimal_schedule& schedule)
{
    using boost::multiprecision::cpp_int;
    const auto interval = binary_fraction(schedule.interval);
    const auto shift = binary_fraction(schedule.shift);
    const cpp_int numerator = (cpp_int(schedule.tick) * interval.first * shift.second +
                                  shift.first * interval.second) *
        duration::period::den;
    const cpp_int denominator = interval.second * shift.second;
    const cpp_int offset = (2 * numerator + denominator) / (2 * denominator);
    const cpp_int ticks = cpp_int(schedule.origin.time_since_epoch().count()) + offset;
    if (ticks > std::numeric_limits<duration::rep>::max() ||
        ticks < std::numeric_limits<duration::rep>::min()) {
        throw error(make_error_code(errc::unsupported_feature),
            "The decimal FMI 3 Clock schedule exceeds the simulation time range");
    }
    return time_point(duration(ticks.convert_to<duration::rep>()));
}

std::optional<time_point> clock_router::legacy_decimal_time(time_point origin, double offset)
{
    const double start = to_double_time_point(origin);
    const double end = start + offset;
    const auto inRange = [](double value) {
        const long double ticks = static_cast<long double>(value) * duration::period::den;
        return std::isfinite(value) &&
            ticks > static_cast<long double>(std::numeric_limits<duration::rep>::min()) &&
            ticks < static_cast<long double>(std::numeric_limits<duration::rep>::max());
    };
    if (!inRange(start) || !inRange(end)) {
        return std::nullopt;
    }
    const boost::multiprecision::cpp_int difference =
        boost::multiprecision::cpp_int(to_time_point(end).time_since_epoch().count()) -
        to_time_point(start).time_since_epoch().count();
    const boost::multiprecision::cpp_int ticks =
        boost::multiprecision::cpp_int(origin.time_since_epoch().count()) + difference;
    if (ticks < std::numeric_limits<duration::rep>::min() ||
        ticks > std::numeric_limits<duration::rep>::max()) {
        return std::nullopt;
    }
    return time_point(duration(ticks.convert_to<duration::rep>()));
}

duration clock_router::legacy_decimal_interval(const decimal_schedule& schedule)
{
    const auto end = legacy_decimal_time(schedule.origin, schedule.interval);
    if (!end) {
        return duration::zero();
    }
    const boost::multiprecision::cpp_int ticks =
        boost::multiprecision::cpp_int(end->time_since_epoch().count()) -
        schedule.origin.time_since_epoch().count();
    if (ticks <= 0 || ticks > std::numeric_limits<duration::rep>::max()) {
        return duration::zero();
    }
    return duration(ticks.convert_to<duration::rep>());
}

std::optional<time_point> clock_router::legacy_decimal_start(const decimal_schedule& schedule)
{
    return legacy_decimal_time(schedule.origin, schedule.shift);
}

time_point clock_router::legacy_decimal_tick(const decimal_schedule& schedule, duration interval)
{
    const auto start = legacy_decimal_start(schedule);
    if (!start || interval <= duration::zero()) {
        throw error(make_error_code(errc::unsupported_feature),
            "The legacy decimal FMI 3 Clock schedule cannot be represented");
    }
    const boost::multiprecision::cpp_int ticks =
        boost::multiprecision::cpp_int(start->time_since_epoch().count()) +
        boost::multiprecision::cpp_int(schedule.tick) * interval.count();
    if (ticks > std::numeric_limits<duration::rep>::max() ||
        ticks < std::numeric_limits<duration::rep>::min()) {
        throw error(make_error_code(errc::unsupported_feature),
            "The legacy decimal FMI 3 Clock schedule exceeds the simulation time range");
    }
    return time_point(duration(ticks.convert_to<duration::rep>()));
}

time_point clock_router::fractional_time(const fractional_schedule& schedule)
{
    using boost::multiprecision::cpp_int;
    const cpp_int counter =
        cpp_int(schedule.shift) + cpp_int(schedule.tick) * schedule.interval;
    cpp_int numerator = counter * duration::period::den;
    cpp_int denominator = schedule.resolution;
    if (schedule.decimal_shift) {
        int exponent = 0;
        const auto mantissa = static_cast<std::uint64_t>(
            std::ldexp(std::frexp(*schedule.decimal_shift, &exponent), binary64_significand_bits));
        cpp_int shiftNumerator = cpp_int(mantissa) * duration::period::den;
        cpp_int shiftDenominator = 1;
        if (exponent >= binary64_significand_bits) {
            shiftNumerator <<= exponent - binary64_significand_bits;
        } else {
            shiftDenominator <<= binary64_significand_bits - exponent;
        }
        numerator = numerator * shiftDenominator + shiftNumerator * denominator;
        denominator *= shiftDenominator;
    }
    const cpp_int offset = (numerator + denominator - 1) / denominator;
    const cpp_int ticks = cpp_int(schedule.origin.time_since_epoch().count()) + offset;
    if (ticks > std::numeric_limits<duration::rep>::max() ||
        ticks < std::numeric_limits<duration::rep>::min()) {
        throw error(make_error_code(errc::unsupported_feature),
            "The fractional FMI 3 Clock schedule exceeds the simulation time range");
    }
    return time_point(duration(ticks.convert_to<duration::rep>()));
}

void clock_router::advance_periodic_clocks(time_point currentTime)
{
    for (auto& clock : periodic_clocks_) {
        while (clock.next <= currentTime) {
            if (clock.fraction) {
                // NOLINTNEXTLINE(bugprone-unchecked-optional-access): The optional is checked above.
                auto& fraction = clock.fraction.value();
                if (fraction.tick == std::numeric_limits<std::uint64_t>::max()) {
                    throw error(make_error_code(errc::unsupported_feature),
                        "The fractional FMI 3 Clock tick counter overflowed");
                }
                ++fraction.tick;
                clock.next = fractional_time(fraction);
            } else if (clock.decimal) {
                // NOLINTNEXTLINE(bugprone-unchecked-optional-access): The optional is checked above.
                auto& decimal = clock.decimal.value();
                if (decimal.tick == std::numeric_limits<std::uint64_t>::max()) {
                    throw error(make_error_code(errc::unsupported_feature),
                        "The decimal FMI 3 Clock tick counter overflowed");
                }
                ++decimal.tick;
                clock.next = decimal.legacy
                    ? legacy_decimal_tick(decimal, clock.interval)
                    : decimal_time(decimal);
            } else {
                throw error(make_error_code(errc::simulation_error),
                    "The periodic FMI 3 Clock schedule has no timing representation");
            }
        }
    }
}

} // namespace cosim
