
/**
 *  \file
 *  Time-related functions and types
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_TIME_HPP
#define COSIM_TIME_HPP

#include <chrono>
#include <cstdint>
#include <ratio>


namespace cosim
{

namespace detail
{

/**
 *  A clock type for simulation time points.
 *
 *  This class fulfils the syntactic requirements of the "Clock" concept
 *  (https://en.cppreference.com/w/cpp/named_req/Clock), but it does not have
 *  any relation to wall-clock time, nor to the logical time of any specific
 *  simulation.  (The `now()` method is therefore deleted.)  Its  sole raison
 *  d'ètre is to be a basis for the definition of `time_point`.
 *
 */
class clock
{
public:
    using rep = std::int64_t;
    using period = std::nano;
    using duration = std::chrono::duration<rep, period>;
    using time_point = std::chrono::time_point<clock>;
    static constexpr bool is_steady = false;
    static time_point now() = delete;
};

} // namespace detail


/// The type used to specify (simulation) time durations.
using duration = detail::clock::duration;


/// The type used to specify (simulation) time points.
using time_point = detail::clock::time_point;


/**
 *  Converts a floating-point number to a `duration`, assuming that the
 *  duration starts at time 0.
 *
 *  For durations that start at a nonzero time point, consider using
 *  `to_duration(double, double)`.
 *
 *  The conversion may be subject to round-off error and truncation.
 */
constexpr duration to_duration(double d)
{
    return std::chrono::duration_cast<duration>(std::chrono::duration<double>(d));
}


/**
 *  Converts a floating-point number to a `time_point`.
 *
 *  The conversion may be subject to round-off error and truncation,
 *  meaning that the relation
 *
 *      to_double_time_point(to_time_point(t)) == t
 *
 *  in general does not hold.
 */
constexpr time_point to_time_point(double t)
{
    return time_point(to_duration(t));
}


/**
 *  Converts a `time_point` to a floating-point number.
 *
 *  The conversion may be subject to round-off error,
 *  meaning that the relation
 *
 *      to_time_point(to_double_time_point(t)) == t
 *
 *  in general does not hold.
 */
constexpr double to_double_time_point(time_point t)
{
    return std::chrono::duration_cast<std::chrono::duration<double>>(
        t.time_since_epoch())
        .count();
}


/**
 *  Converts a floating-point number to a `duration`.
 *
 *  The conversion is done in such a way as to preserve addition of a
 *  duration to a time point.  In other words, if `t1 + d == t2`, then
 *
 *      to_time_point(t1) + to_duration(d, t1) == to_time_point(t2)
 *
 *  Since the precision of a floating-point number depends on its absolute
 *  value, the start time of the duration is required for this calculation.
 *
 *  The conversion may be subject to round-off error and truncation,
 *  meaning that the relation
 *
 *      to_double_duration(to_duration(d, t), to_time_point(t)) == d
 *
 *  in general does not hold.
 */
constexpr duration to_duration(double d, double startTime)
{
    return to_time_point(startTime + d) - to_time_point(startTime);
}


/**
 *  Converts a `duration` to a floating-point number.
 *
 *  The conversion in done in such a way as to preserve addition of a
 *  duration to a time point.  In other words, if `t1 + d == t2`, then
 *
 *      to_double_time_point(t1) + to_double_duration(d, t1) == to_double_time_point(t2)
 *
 *  Since the precision of a floating-point number depends on its absolute
 *  value, the start time of the duration is required for this calculation.
 *
 *  The conversion may be subject to round-off error, meaning that the
 *  relation
 *
 *      to_duration(to_double_duration(d, t), to_double_time_point(t)) == d
 *
 *  in general does not hold.
 */
constexpr double to_double_duration(duration d, time_point startTime)
{
    return to_double_time_point(startTime + d) - to_double_time_point(startTime);
}


} // namespace cosim
#endif // header guard
