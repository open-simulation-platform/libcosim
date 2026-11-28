/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "clock_router.hpp"

#include "cosim/exception.hpp"
#include "cosim/model_description.hpp"
#include "cosim/serialization.hpp"
#include "cosim/slave.hpp"

#include <boost/algorithm/cxx11/any_of.hpp>
#include <boost/multiprecision/cpp_int.hpp> // IWYU pragma: keep
#include <boost/property_tree/exceptions.hpp>
#include <boost/range/algorithm/find_if.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace cosim
{

namespace
{
constexpr std::uint32_t exact_clock_origin_state_version = 5;
constexpr std::uint32_t tunable_clock_state_version = 6;
constexpr std::uint32_t current_clock_state_version = tunable_clock_state_version;
constexpr std::size_t max_clock_integer_digits = 1260;
constexpr int decimal_base = 10;
} // namespace

serialization::node clock_router::export_state() const
{
    check_recovery();
    if (terminated_) {
        throw error(make_error_code(errc::unsupported_feature),
            "Saving terminated Clock coordination is not supported");
    }
    serialization::node state;
    state.put<std::uint32_t>("version", current_clock_state_version);
    state.put<std::int64_t>("start_time_ticks", start_time_.time_since_epoch().count());
    state.put("has_next_event", next_event_time_.has_value());
    if (next_event_time_) {
        state.put<std::int64_t>("next_event_ticks", next_event_time_->time_since_epoch().count());
    }
    const auto exportConnections = [](const auto& connections) {
        serialization::node result;
        for (std::size_t i = 0; i < connections.size(); ++i) {
            const auto& connection = connections.at(i);
            serialization::node entry;
            entry.put<std::int32_t>("source_simulator", connection.source.simulator);
            entry.put<std::int32_t>("source_type", static_cast<std::int32_t>(connection.source.type));
            entry.put<std::uint32_t>("source_reference", connection.source.reference);
            entry.put<std::int32_t>("target_simulator", connection.target.simulator);
            entry.put<std::int32_t>("target_type", static_cast<std::int32_t>(connection.target.type));
            entry.put<std::uint32_t>("target_reference", connection.target.reference);
            result.put_child(std::to_string(i), entry);
        }
        return result;
    };
    state.put_child("clock_connections", exportConnections(clock_connections_));
    state.put_child("data_connections", exportConnections(clocked_data_connections_));
    serialization::node clocks;
    for (std::size_t i = 0; i < periodic_clocks_.size(); ++i) {
        const auto& clock = periodic_clocks_.at(i);
        serialization::node entry;
        entry.put<std::int32_t>("simulator", clock.id.simulator);
        entry.put<std::uint32_t>("reference", clock.id.reference);
        entry.put<std::int64_t>("interval_ticks", clock.interval.count());
        entry.put<std::int64_t>("next_ticks", clock.next.time_since_epoch().count());
        entry.put("fractional", clock.fraction.has_value());
        if (clock.fraction) {
            const auto& fraction = *clock.fraction;
            entry.put<std::int64_t>("origin_ticks", fraction.origin.time_since_epoch().count());
            entry.put<std::uint64_t>("interval_counter", fraction.interval);
            entry.put<std::uint64_t>("resolution", fraction.resolution);
            entry.put<std::uint64_t>("shift_counter", fraction.shift);
            entry.put<std::uint64_t>("tick", fraction.tick);
            entry.put("has_decimal_shift", fraction.decimal_shift.has_value());
            if (fraction.decimal_shift) {
                entry.put("decimal_shift", *fraction.decimal_shift);
            }
        } else if (clock.decimal) {
            const auto& decimal = *clock.decimal;
            entry.put<std::int64_t>("origin_ticks", decimal.origin.time_since_epoch().count());
            entry.put("interval_decimal", decimal.interval);
            entry.put("shift_decimal", decimal.shift);
            entry.put<std::uint64_t>("tick", decimal.tick);
            entry.put("legacy_decimal", decimal.legacy);
        }
        clocks.put_child(std::to_string(i), entry);
    }
    state.put_child("periodic_clocks", clocks);
    serialization::node changingClocks;
    for (std::size_t i = 0; i < changing_clocks_.size(); ++i) {
        const auto& clock = changing_clocks_.at(i);
        serialization::node entry;
        entry.put<std::int32_t>("simulator", clock.id.simulator);
        entry.put<std::uint32_t>("reference", clock.id.reference);
        entry.put<std::int64_t>("origin_ticks", clock.origin.time_since_epoch().count());
        entry.put("interval_decimal", clock.interval.interval);
        entry.put("countdown", clock.countdown);
        entry.put("known", clock.known);
        entry.put("tunable", clock.tunable);
        entry.put("ticked", clock.ticked);
        entry.put("origin_numerator", clock.origin_numerator.convert_to<std::string>());
        entry.put("origin_denominator", clock.origin_denominator.convert_to<std::string>());
        entry.put("fractional", clock.interval.counter.has_value());
        if (clock.interval.counter) {
            entry.put<std::uint64_t>("counter", clock.interval.counter.value());
            entry.put<std::uint64_t>("resolution", clock.interval.resolution.value()); // NOLINT(bugprone-unchecked-optional-access): Fractional intervals carry both a counter and a resolution.
        }
        entry.put("has_next", clock.next.has_value());
        if (clock.next) {
            entry.put<std::int64_t>("next_ticks", clock.next->time_since_epoch().count());
        }
        changingClocks.put_child(std::to_string(i), entry);
    }
    state.put_child("changing_clocks", changingClocks);
    return state;
}

void clock_router::import_state(const serialization::node& state)
{
    try {
        const auto version = state.get<std::uint32_t>("version");
        if ((version < 1 || version > current_clock_state_version) ||
            state.get<std::int64_t>("start_time_ticks") != start_time_.time_since_epoch().count()) {
            throw error(make_error_code(errc::bad_file),
                "The serialized Clock coordination configuration is incompatible");
        }
        const auto checkConnections = [](const serialization::node& saved, const auto& connections) {
            if (saved.size() != connections.size()) {
                return false;
            }
            for (std::size_t i = 0; i < connections.size(); ++i) {
                const auto& entry = saved.get_child(std::to_string(i));
                const auto& connection = connections.at(i);
                if (entry.get<std::int32_t>("source_simulator") != connection.source.simulator ||
                    entry.get<std::int32_t>("source_type") != static_cast<std::int32_t>(connection.source.type) ||
                    entry.get<std::uint32_t>("source_reference") != connection.source.reference ||
                    entry.get<std::int32_t>("target_simulator") != connection.target.simulator ||
                    entry.get<std::int32_t>("target_type") != static_cast<std::int32_t>(connection.target.type) ||
                    entry.get<std::uint32_t>("target_reference") != connection.target.reference) {
                    return false;
                }
            }
            return true;
        };
        if (!checkConnections(state.get_child("clock_connections"), clock_connections_) ||
            !checkConnections(state.get_child("data_connections"), clocked_data_connections_)) {
            throw error(make_error_code(errc::bad_file),
                "The serialized Clock connections are incompatible");
        }
        auto clocks = parse_periodic_clock_state(state, version);
        auto changing = parse_changing_clock_state(state, version);
        commit_clock_state(state, std::move(clocks), std::move(changing));
    } catch (const boost::property_tree::ptree_error&) {
        throw error(make_error_code(errc::bad_file),
            "The serialized Clock coordination state is invalid or corrupt");
    } catch (const std::bad_variant_access&) {
        throw error(make_error_code(errc::bad_file),
            "The serialized Clock coordination state is invalid or corrupt");
    }
}

std::vector<clock_router::periodic_clock> clock_router::parse_periodic_clock_state(
    const serialization::node& state, std::uint32_t version) const
{
    auto clocks = periodic_clocks_;
    const auto& savedClocks = state.get_child("periodic_clocks");
    if (savedClocks.size() != clocks.size()) {
        throw error(make_error_code(errc::bad_file),
            "The serialized periodic Clocks are incompatible");
    }
    std::unordered_set<variable_id> seen;
    for (std::size_t i = 0; i < savedClocks.size(); ++i) {
        const auto& entry = savedClocks.get_child(std::to_string(i));
        const variable_id identifier{
            entry.get<std::int32_t>("simulator"),
            variable_type::clock,
            entry.get<std::uint32_t>("reference"),
        };
        const auto clock = boost::range::find_if(clocks,
            [identifier](const auto& candidate) { return candidate.id == identifier; });
        if (clock == clocks.end() || !seen.insert(identifier).second ||
            entry.get<std::int64_t>("interval_ticks") != clock->interval.count() ||
            entry.get<bool>("fractional") != clock->fraction.has_value()) {
            throw error(make_error_code(errc::bad_file),
                "The serialized periodic Clock configuration is incompatible");
        }
        const auto next = time_point(duration(entry.get<std::int64_t>("next_ticks")));
        if (next < start_time_) {
            throw error(make_error_code(errc::bad_file),
                "The serialized periodic Clock time is invalid");
        }
        if (clock->fraction) {
            restore_fractional_clock(entry, next, *clock->fraction);
        } else {
            restore_decimal_clock(entry, version, next, clock->interval, *clock->decimal); // NOLINT(bugprone-unchecked-optional-access): A non-fractional periodic schedule is decimal.
        }
        clock->next = next;
    }
    return clocks;
}

void clock_router::restore_fractional_clock(
    const serialization::node& entry, time_point next, fractional_schedule& schedule)
{
    auto fraction = schedule;
    const auto hasDecimalShift = entry.get<bool>("has_decimal_shift");
    if (entry.get<std::int64_t>("origin_ticks") != fraction.origin.time_since_epoch().count() ||
        entry.get<std::uint64_t>("interval_counter") != fraction.interval ||
        entry.get<std::uint64_t>("resolution") != fraction.resolution ||
        entry.get<std::uint64_t>("shift_counter") != fraction.shift ||
        hasDecimalShift != fraction.decimal_shift.has_value() ||
        (hasDecimalShift && entry.get<double>("decimal_shift") != *fraction.decimal_shift)) {
        throw error(make_error_code(errc::bad_file),
            "The serialized fractional Clock configuration is incompatible");
    }
    fraction.tick = entry.get<std::uint64_t>("tick");
    time_point calculated;
    try {
        calculated = fractional_time(fraction);
    } catch (const error&) {
        throw error(make_error_code(errc::bad_file),
            "The serialized fractional Clock time is invalid");
    }
    if (calculated != next) {
        throw error(make_error_code(errc::bad_file),
            "The serialized fractional Clock time is inconsistent");
    }
    schedule = fraction;
}

void clock_router::restore_decimal_clock(
    const serialization::node& entry, std::uint32_t version,
    time_point next, duration interval, decimal_schedule& schedule)
{
    using boost::multiprecision::cpp_int;
    auto decimal = schedule;
    if (version >= 2) {
        if (entry.get<std::int64_t>("origin_ticks") != decimal.origin.time_since_epoch().count() ||
            entry.get<double>("interval_decimal") != decimal.interval ||
            entry.get<double>("shift_decimal") != decimal.shift) {
            throw error(make_error_code(errc::bad_file),
                "The serialized decimal Clock configuration is incompatible");
        }
        decimal.tick = entry.get<std::uint64_t>("tick");
        decimal.legacy = entry.get("legacy_decimal", false);
    } else {
        const auto legacyStart = legacy_decimal_start(decimal);
        if (!legacyStart || interval <= duration::zero()) {
            throw error(make_error_code(errc::bad_file),
                "The serialized legacy decimal Clock schedule cannot be restored");
        }
        const cpp_int difference = cpp_int(next.time_since_epoch().count()) -
            legacyStart->time_since_epoch().count();
        const cpp_int tick = difference / interval.count();
        if (difference < 0 || difference % interval.count() != 0 ||
            tick > std::numeric_limits<std::uint64_t>::max()) {
            throw error(make_error_code(errc::bad_file),
                "The serialized legacy decimal Clock time is inconsistent");
        }
        decimal.tick = tick.convert_to<std::uint64_t>();
        decimal.legacy = true;
    }
    time_point calculated;
    try {
        calculated = decimal.legacy
            ? legacy_decimal_tick(decimal, interval)
            : decimal_time(decimal);
    } catch (const error&) {
        throw error(make_error_code(errc::bad_file),
            "The serialized decimal Clock time is invalid");
    }
    if (calculated != next) {
        throw error(make_error_code(errc::bad_file),
            "The serialized periodic Clock time is inconsistent");
    }
    schedule = decimal;
}

std::vector<clock_router::changing_clock> clock_router::parse_changing_clock_state(
    const serialization::node& state, std::uint32_t version) const
{
    auto changing = changing_clocks_;
    if (version < 3 && !changing.empty()) {
        throw error(make_error_code(errc::bad_file),
            "The serialized Clock coordination is missing changing schedules");
    }
    if (version < 4 && boost::algorithm::any_of(changing, [](const auto& clock) { return clock.countdown; })) {
        throw error(make_error_code(errc::bad_file),
            "The serialized Clock coordination is missing countdown schedules");
    }
    if (version < tunable_clock_state_version && boost::algorithm::any_of(changing, [](const auto& clock) { return clock.tunable; })) {
        throw error(make_error_code(errc::bad_file),
            "The serialized Clock coordination is missing tunable schedules");
    }
    if (version >= 3) {
        const auto& savedChanging = state.get_child("changing_clocks");
        if (savedChanging.size() != changing.size()) {
            throw error(make_error_code(errc::bad_file),
                "The serialized changing Clocks are incompatible");
        }
        std::unordered_set<variable_id> seenChanging;
        for (std::size_t i = 0; i < savedChanging.size(); ++i) {
            const auto& entry = savedChanging.get_child(std::to_string(i));
            const variable_id identifier{
                entry.get<std::int32_t>("simulator"),
                variable_type::clock,
                entry.get<std::uint32_t>("reference"),
            };
            const auto clock = boost::range::find_if(changing,
                [identifier](const auto& candidate) { return candidate.id == identifier; });
            if (clock == changing.end() || !seenChanging.insert(identifier).second) {
                throw error(make_error_code(errc::bad_file),
                    "The serialized changing Clock is incompatible");
            }
            restore_changing_clock(entry, version, *clock);
        }
    }
    return changing;
}

void clock_router::restore_changing_clock(
    const serialization::node& entry, std::uint32_t version, changing_clock& clock) const
{
    const bool known = version >= 4 ? entry.get<bool>("known") : true;
    if (version >= 4 && entry.get<bool>("countdown") != clock.countdown) {
        throw error(make_error_code(errc::bad_file),
            "The serialized aperiodic Clock type is incompatible");
    }
    if (version >= tunable_clock_state_version && entry.get<bool>("tunable") != clock.tunable) {
        throw error(make_error_code(errc::bad_file),
            "The serialized tunable Clock type is incompatible");
    }
    const auto origin = time_point(duration(entry.get<std::int64_t>("origin_ticks")));
    if (origin < start_time_) {
        throw error(make_error_code(errc::bad_file),
            "The serialized changing Clock origin is invalid");
    }
    clock_interval_update interval{
        clock_interval_qualifier::changed,
        entry.get<double>("interval_decimal"),
        {},
        {},
    };
    const bool fractional = entry.get<bool>("fractional");
    if (fractional != clock.interval.counter.has_value()) {
        throw error(make_error_code(errc::bad_file),
            "The serialized changing Clock representation is incompatible");
    }
    if (fractional) {
        interval.counter = entry.get<std::uint64_t>("counter");
        interval.resolution = entry.get<std::uint64_t>("resolution");
    }
    std::optional<time_point> next;
    if (entry.get<bool>("has_next")) {
        next = time_point(duration(entry.get<std::int64_t>("next_ticks")));
    } else if (entry.get_optional<std::int64_t>("next_ticks")) {
        throw error(make_error_code(errc::bad_file),
            "The serialized changing Clock time is inconsistent");
    }
    if ((!clock.countdown && !known) || (!known && next) ||
        (clock.tunable && !next)) {
        throw error(make_error_code(errc::bad_file),
            "The serialized aperiodic Clock qualifier is invalid");
    }
    auto restored = clock;
    restored.origin = origin;
    restored.interval = interval;
    restored.ticked = version >= tunable_clock_state_version && entry.get<bool>("ticked");
    if (version >= exact_clock_origin_state_version) {
        restored.origin_numerator =
            parse_clock_integer(entry.get<std::string>("origin_numerator"), true);
        restored.origin_denominator =
            parse_clock_integer(entry.get<std::string>("origin_denominator"), false);
    } else {
        restored.origin_numerator = origin.time_since_epoch().count();
        restored.origin_denominator = 1;
    }
    time_point calculated;
    try {
        if (rounded_clock_time(restored.origin_numerator, restored.origin_denominator,
                restored.interval.counter.has_value()) != origin) {
            throw error(make_error_code(errc::bad_file),
                "The serialized aperiodic Clock origin is inconsistent");
        }
        if (restored.tunable) {
            validate_tunable_interval(restored.interval);
        }
        calculated = restored.tunable && !restored.ticked
            ? restored.origin
            : changing_time(restored);
    } catch (const error&) {
        throw error(make_error_code(errc::bad_file),
            "The serialized changing Clock interval is invalid");
    }
    if (next && *next != calculated) {
        throw error(make_error_code(errc::bad_file),
            "The serialized changing Clock time is inconsistent");
    }
    restored.next = next;
    restored.known = known;
    clock = std::move(restored);
}

void clock_router::commit_clock_state(
    const serialization::node& state, std::vector<periodic_clock> clocks,
    std::vector<changing_clock> changing)
{
    std::optional<time_point> nextEvent;
    if (state.get<bool>("has_next_event")) {
        nextEvent = time_point(duration(state.get<std::int64_t>("next_event_ticks")));
        if (*nextEvent < start_time_) {
            throw error(make_error_code(errc::bad_file),
                "The serialized next Clock event is invalid");
        }
    } else if (state.get_optional<std::int64_t>("next_event_ticks")) {
        throw error(make_error_code(errc::bad_file),
            "The serialized next Clock event is inconsistent");
    }
    for (const auto& clock : clocks) {
        if (!nextEvent || *nextEvent > clock.next) {
            throw error(make_error_code(errc::bad_file),
                "The serialized next Clock event is inconsistent");
        }
    }
    for (const auto& clock : changing) {
        if (clock.next && (!nextEvent || *nextEvent > *clock.next)) {
            throw error(make_error_code(errc::bad_file),
                "The serialized next Clock event is inconsistent");
        }
    }
    periodic_clocks_ = std::move(clocks);
    changing_clocks_ = std::move(changing);
    next_event_time_ = nextEvent;
    terminated_ = false;
    recovery_required_ = false;
}

boost::multiprecision::cpp_int clock_router::parse_clock_integer(
    const std::string& value, bool signedValue)
{
    const auto first = signedValue && !value.empty() && value.front() == '-' ? 1U : 0U;
    if (value.size() <= first || value.size() > max_clock_integer_digits ||
        !std::all_of(value.begin() + first, value.end(),
            [](char character) { return character >= '0' && character <= '9'; })) {
        throw error(make_error_code(errc::bad_file),
            "The serialized aperiodic Clock fraction is invalid");
    }
    boost::multiprecision::cpp_int result = 0;
    for (auto i = first; i < value.size(); ++i) {
        result = result * decimal_base + (value.at(i) - '0');
    }
    if (first != 0U) {
        result = -result;
    }
    if (!signedValue && result == 0) {
        throw error(make_error_code(errc::bad_file),
            "The serialized aperiodic Clock denominator is invalid");
    }
    if (!signedValue && boost::multiprecision::msb(result) > max_clock_denominator_msb) { // NOLINT(misc-include-cleaner)
        throw error(make_error_code(errc::bad_file),
            "The serialized aperiodic Clock denominator is too large");
    }
    return result;
}

} // namespace cosim
