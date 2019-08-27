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
#include <optional>
#include <ostream>
#include <ratio>
#include <string>
#include <utility>
#include <variant>
#include <vector>


namespace cse
{

namespace detail
{

/*
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


/// Unsigned integer type used for variable identifiers.
using value_reference = std::uint32_t;


/// Variable data types.
enum class variable_type
{
    real,
    integer,
    boolean,
    string,
    enumeration
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
        case variable_type::enumeration: return "enumeration";
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
    value_reference reference;

    /// The variable's data type.
    variable_type type;

    /// The variable's causality.
    variable_causality causality;

    /// The variable's variability.
    variable_variability variability;

    /// The variable's start value.
    std::optional<std::variant<double, int, bool, std::string>> start;
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

/// Getter for returning a variable description.
const variable_description find_variable(const model_description& description, const std::string& variable_name);

/// Getter for returning a variable description.
const variable_description find_variable(const model_description& description, variable_type type, value_reference reference);

/// Getter for returning all variable descriptions of the given datatype.
const std::vector<variable_description> find_variables_of_type(const model_description& description, variable_type type);


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
