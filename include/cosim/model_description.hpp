/**
 *  \file
 *  Model-descriptive types and constants.
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_MODEL_HPP
#define COSIM_MODEL_HPP

#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>


namespace cosim
{


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

/// A list of simulator capabilities
struct simulator_capabilities
{
    bool can_get_and_set_fmu_state{};
    bool can_serialize_fmu_state{};
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


/**
 *  An algebraic type that can hold a scalar value of one of the supported
 *  variable types.
 */
using scalar_value = std::variant<double, int, bool, std::string>;


/**
 *  An algebraic type that can hold a (possibly) non-owning, read-only view
 *  of a scalar value of one of the supported variable types.
 *
 *  In practice, it's only for strings that this type is a view; for all
 *  other types it holds a copy.
 */
using scalar_value_view = std::variant<double, int, bool, std::string_view>;


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
    std::optional<scalar_value> start;
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

    // Simulator capabilities
    simulator_capabilities capabilities;
};

/// Getter for returning a variable description.
std::optional<variable_description> find_variable(const model_description& description, const std::string& variable_name);

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


} // namespace cosim
#endif // COSIM_MODEL_HPP
