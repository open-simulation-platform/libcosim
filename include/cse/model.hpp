/**
 *  \file
 *  Model-descriptive types and constants.
 */
#ifndef CSE_MODEL_HPP
#define CSE_MODEL_HPP

#include <chrono>
#include <cstdint>
#include <iterator>
#include <limits>
#include <ostream>
#include <ratio>
#include <string>
#include <utility>
#include <vector>


namespace cse
{


namespace detail
{
class clock
{
public:
    using rep = std::int64_t;
    using period = std::nano;
    using duration = std::chrono::duration<rep, period>;
    using time_point = std::chrono::time_point<clock>;
    static constexpr bool is_steady = true;
    static time_point now() = delete;
};
} // namespace detail


/// The type used to specify (simulation) time durations.
using duration = detail::clock::duration;


/// The type used to specify (simulation) time points.
using time_point = detail::clock::time_point;


/**
 *  Converts a floating-point "model duration" to a `duration`,
 *  assuming the duration starts at time 0.
 */
constexpr duration to_duration(double modelDuration)
{
    return std::chrono::duration_cast<duration>(std::chrono::duration<double>(modelDuration));
}


/**
 *  Converts a floating-point "model time point" to a `time_point`.
 *
 *  The conversion may cause loss of precision, meaning that the relation
 *
 *      to_model_time_point(to_time_point(t, r)) == t
 *
 *  does *not* necessarily hold.
 */
constexpr time_point to_time_point(double modelTimePoint) noexcept
{
    return time_point(to_duration(modelTimePoint));
}


/// Converts a `time_point` to a floating-point "model time point".
constexpr double to_model_time_point(time_point t) noexcept
{
    return std::chrono::duration_cast<std::chrono::duration<double>>(t.time_since_epoch()).count();
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
    const auto endTime = to_time_point(modelEndTime);
    return endTime - startTime;
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
