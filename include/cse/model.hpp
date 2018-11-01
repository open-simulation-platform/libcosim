/**
 *  \file
 *  Model-descriptive types and constants.
 */
#ifndef CSE_MODEL_HPP
#define CSE_MODEL_HPP

#include <cassert>
#include <cstdint>
#include <iterator>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>


namespace cse
{

/**
 *  The default time resolution used for `time_point` and `duration` when
 *  nothing else is specified.
 *
 *  The value is currently `1e-9`.  As time points and durations are stored as
 *  64-bit signed integers that represent some multiple of the time resolution,
 *  the range of representable model time values is roughly -9,223,372,036 to
 *  +9,223,372,036.
 */
constexpr double default_time_resolution = 1e-9;


namespace detail
{
    template<typename T, typename U>
    constexpr void time_resolution_check(T a, U b)
    {
        if (a.resolution() != b.resolution()) {
            throw std::logic_error(
                "Attempted to perform binary operation on time quantities "
                "with different resolutions");
        }
    }
}

class duration;


/**
 *  The type used to specify (simulation) time points.
 *
 *  This is a signed integer type which represents time as a multiple of some
 *  time resolution.  It is recommended to use `model_time_point()` to convert
 *  a `time_point` to actual model time.
 */
class time_point
{
public:
    constexpr explicit time_point(
        std::int64_t count = 0,
        double resolution = default_time_resolution) noexcept
        : count_(count), resolution_(resolution)
    {
        assert(resolution > 0.0);
    }

    constexpr std::int64_t count() const noexcept { return count_; }

    constexpr double resolution() const noexcept { return resolution_; }

    // Defined below
    constexpr time_point& operator+=(const duration& dt);
    constexpr time_point& operator-=(const duration& dt);

private:
    std::int64_t count_;
    double resolution_;
};


/// The type used to specify (simulation) time durations.
class duration
{
public:
    constexpr explicit duration(
        std::int64_t count = 0,
        double resolution = default_time_resolution) noexcept
        : count_(count), resolution_(resolution)
    {
        assert(resolution > 0.0);
    }

    constexpr std::int64_t count() const noexcept { return count_; }

    constexpr double resolution() const noexcept { return resolution_; }

    constexpr duration operator-() const
    {
        return duration(-count_, resolution_);
    }

    constexpr duration operator+() const { return *this; }

    // In-place arithmetics
    constexpr duration& operator+=(duration other)
    {
        detail::time_resolution_check(*this, other);
        count_ += other.count_;
        return *this;
    }

    constexpr duration& operator-=(duration other)
    {
        detail::time_resolution_check(*this, other);
        count_ -= other.count_;
        return *this;
    }

    constexpr duration& operator*=(std::int64_t a)
    {
        count_ *= a;
        return *this;
    }

private:
    std::int64_t count_;
    double resolution_;
};


// Time point comparisons
constexpr bool operator==(time_point a, time_point b)
{
    detail::time_resolution_check(a, b);
    return a.count() == b.count();
}

constexpr bool operator !=(time_point a, time_point b)
{
    detail::time_resolution_check(a, b);
    return a.count() != b.count();
}

constexpr bool operator<(time_point a, time_point b)
{
    detail::time_resolution_check(a, b);
    return a.count() < b.count();
}

constexpr bool operator<=(time_point a, time_point b)
{
    detail::time_resolution_check(a, b);
    return a.count() <= b.count();
}

constexpr bool operator>(time_point a, time_point b)
{
    detail::time_resolution_check(a, b);
    return a.count() >= b.count();
}

constexpr bool operator>=(time_point a, time_point b)
{
    detail::time_resolution_check(a, b);
    return a.count() >= b.count();
}

// Duration comparisons
constexpr bool operator==(duration a, duration b)
{
    detail::time_resolution_check(a, b);
    return a.count() == b.count();
}

constexpr bool operator !=(duration a, duration b)
{
    detail::time_resolution_check(a, b);
    return a.count() != b.count();
}

constexpr bool operator<(duration a, duration b)
{
    detail::time_resolution_check(a, b);
    return a.count() < b.count();
}

constexpr bool operator<=(duration a, duration b)
{
    detail::time_resolution_check(a, b);
    return a.count() <= b.count();
}

constexpr bool operator>(duration a, duration b)
{
    detail::time_resolution_check(a, b);
    return a.count() >= b.count();
}

constexpr bool operator>=(duration a, duration b)
{
    detail::time_resolution_check(a, b);
    return a.count() >= b.count();
}

// Duration arithmetics
constexpr duration operator+(duration a, duration b)
{
    return a += b;
}

constexpr duration operator-(duration a, duration b)
{
    return a -= b;
}

constexpr duration operator*(duration a, std::int64_t b)
{
    return a *= b;
}

constexpr duration operator*(std::int64_t a, duration b)
{
    return b *= a;
}

// Time point-duration arithmetics
constexpr time_point& time_point::operator+=(const duration& d)
{
    detail::time_resolution_check(*this, d);
    count_ += d.count();
    return *this;
}

constexpr time_point& time_point::operator-=(const duration& d)
{
    detail::time_resolution_check(*this, d);
    count_ -= d.count();
    return *this;
}

constexpr time_point operator+(time_point t, duration d)
{
    return t += d;
}

constexpr time_point operator-(time_point t, duration d)
{
    return t -= d;
}

constexpr duration operator-(time_point t1, time_point t2)
{
    detail::time_resolution_check(t1, t2);
    return duration(t1.count() - t2.count(), t1.resolution());
}


/// Returns the absolute value of a `duration`.
constexpr duration abs(duration d) noexcept
{
    return d.count() >= 0 ? d : -d;
}


/**
 *  Converts a floating-point "model time point" to a `time_point`.
 *
 *  The conversion is lossy if the result of the floating-point operation
 *  `modelTimePoint/resolution` is not an integer.  In other words, the
 *  relation
 *
 *      to_model_time_point(to_time_point(t, r)) == t
 *
 *  does *not* necessarily hold.
 */
constexpr time_point to_time_point(
    double modelTimePoint,
    double resolution = default_time_resolution)
{
    return time_point(
        static_cast<std::int64_t>(modelTimePoint/resolution),
        resolution);
}


/**
 *  Converts a `time_point` to a floating-point "model time point".
 *
 *  The result is simply the product of `t.count()` and `t.resolution()`.
 */
constexpr double to_model_time_point(time_point t) noexcept
{
    return t.count() * t.resolution();
}


/**
 *  Converts a floating-point "model duration" to a `duration`.
 *
 *  The conversion in done in such a way as to preserve addition of a
 *  duration to a time point.  That is, the relation
 *
 *      t1 + to_duration(md, t1) == t2
 *
 *  holds if and only if
 *
 *      to_model_time_point(t1) + md == to_model_time_point(t2)
 *
 *  where `t1` and `t2` are both of type `time_point` and `md` is of
 *  type `double`.
 *
 *  Since the precision of a floating-point number depends on its absolute
 *  value, the start time of the duration is required for this calculation.
 */
constexpr duration to_duration(double modelDuration, time_point startTime)
{
    const auto modelStartTime = to_model_time_point(startTime);
    const auto modelEndTime = modelStartTime + modelDuration;
    const auto endTime = to_time_point(modelEndTime, startTime.resolution());
    return endTime - startTime;
}


/**
 *  Converts a floating-point "model duration" to a `duration`,
 *  assuming the duration starts at time 0.
 *
 *  This is a convenience function which is equivalent to:
 *
 *      to_duration(modelDuration, time_point(0, resolution));
 */
constexpr duration to_duration(
    double modelDuration,
    double resolution = default_time_resolution)
{
    return to_duration(modelDuration, time_point(0, resolution));
}


/**
 *  Converts a floating-point "model duration" to a `duration`.
 *
 *  The conversion in done in such a way as to preserve addition of a
 *  duration to a time point.  That is, the relation
 *
 *      to_model_time_point(t1) + to_model_duration(d, t1) == to_model_time_point(t2)
 *
 *  holds if and only if
 *
 *      t1 + d == t2
 *
 *  where `t1` and `t2` are both of type `time_point` and `d` is of
 *  type `duration`.
 *
 *  Since the precision of a floating-point number depends on its absolute
 *  value, the start time of the duration is required for this calculation.
 */
constexpr double to_model_duration(duration d, time_point startTime)
{
    return to_model_time_point(startTime + d) - to_model_time_point(startTime);
}


/// Unsigned integer type used for variable identifiers.
using variable_index = std::uint32_t;


/// Variable data types.
enum class variable_type
{
    real,
    integer,
    boolean,
    string
};


/// Variable causalities.  These correspond to FMI causality definitions.
enum variable_causality
{
    parameter,
    calculated_parameter,
    input,
    output,
    local
};


/// Variable variabilities.  These correspond to FMI variability definitions.
enum variable_variability
{
    constant,
    fixed,
    tunable,
    discrete,
    continuous
};


/// Returns a textual representation of `v`.
constexpr const char* to_text(variable_type v)
{
    switch (v) {
        case variable_type::real: return "real";
        case variable_type::integer: return "integer";
        case variable_type::boolean: return "boolean";
        case variable_type::string: return "string";
        default: return nullptr;
    }
}


/// Returns a textual representation of `v`.
constexpr const char* to_text(variable_causality v)
{
    switch (v) {
        case variable_causality::parameter: return "parameter";
        case variable_causality::calculated_parameter: return "calculated_parameter";
        case variable_causality::input: return "input";
        case variable_causality::output: return "output";
        case variable_causality::local: return "local";
        default: return nullptr;
    }
}


/// Returns a textual representation of `v`.
constexpr const char* to_text(variable_variability v)
{
    switch (v) {
        case variable_variability::constant: return "constant";
        case variable_variability::fixed: return "fixed";
        case variable_variability::tunable: return "tunable";
        case variable_variability::discrete: return "discrete";
        case variable_variability::continuous: return "continuous";
        default: return nullptr;
    }
}


/// Writes a textual representation of `v` to `stream`.
inline std::ostream& operator<<(std::ostream& stream, variable_type v)
{
    return stream << to_text(v);
}


/// Writes a textual representation of `v` to `stream`.
inline std::ostream& operator<<(std::ostream& stream, variable_causality v)
{
    return stream << to_text(v);
}


/// Writes a textual representation of `v` to `stream`.
inline std::ostream& operator<<(std::ostream& stream, variable_variability v)
{
    return stream << to_text(v);
}


/// A description of a model variable.
struct variable_description
{
    /**
     *  A textual identifier for the variable.
     *
     *  The name must be unique within the model.
     */
    std::string name;

    /**
     *  A numerical identifier for the value the variable refers to.
     *
     *  The variable reference must be unique within the model and data type.
     *  That is, a real variable and an integer variable may have the same
     *  value reference, and they will be considered different. If two
     *  variables of the same type have the same value reference, they will
     *  be considered as aliases of each other.
     */
    variable_index index;

    /// The variable's data type.
    variable_type type;

    /// The variable's causality.
    variable_causality causality;

    /// The variable's variability.
    variable_variability variability;
};


/// A description of a model.
struct model_description
{
    /// The model name.
    std::string name;

    /// A universally unique identifier (UUID) for the model.
    std::string uuid;

    /// A human-readable description of the model.
    std::string description;

    /// Author information.
    std::string author;

    /// Version information.
    std::string version;

    /// Variable descriptions.
    std::vector<variable_description> variables;
};


/// Possible outcomes of a subsimulator time step
enum class step_result
{
    /// Step completed
    complete,

    /// Step failed, but can be retried with a shorter step size
    failed,

    /// Step canceled
    canceled
};


} // namespace cse
#endif // header guard
