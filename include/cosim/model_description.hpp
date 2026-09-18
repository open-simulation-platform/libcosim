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

#include <cosim/time.hpp>

#include <cstddef>
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
    /// FMI 3 Float64.
    real,
    /// FMI 3 Int32 and the legacy FMI integer type.
    integer,
    /// FMI Boolean.
    boolean,
    /// FMI String.
    string,
    /// FMI Enumeration.
    enumeration,
    /// FMI Float32.
    float32,
    /// FMI Int8.
    int8,
    /// FMI UInt8.
    uint8,
    /// FMI Int16.
    int16,
    /// FMI UInt16.
    uint16,
    /// FMI UInt32.
    uint32,
    /// FMI Int64.
    int64,
    /// FMI UInt64.
    uint64,
    /// FMI Binary.
    binary,
    /// FMI Clock.
    clock
};


/// Variable causalities.  These correspond to FMI causality definitions.
enum variable_causality
{
    /// A parameter fixed before simulation starts.
    parameter,
    /// A parameter calculated during initialization.
    calculated_parameter,
    /// A value supplied by another model or the environment.
    input,
    /// A value produced by the model.
    output,
    /// An internal model variable.
    local,
    /// A parameter that determines an array shape.
    structural_parameter,
    /// An independent variable such as simulation time.
    independent
};


/// Variable variabilities.  These correspond to FMI variability definitions.
enum variable_variability
{
    /// The value cannot change during a simulation.
    constant,
    /// The value is fixed after initialization.
    fixed,
    /// The value may be changed during initialization.
    tunable,
    /// The value may change only at discrete instants.
    discrete,
    /// The value may change continuously.
    continuous
};

/// A list of simulator capabilities
struct simulator_capabilities
{
    /// Whether the simulator can create and restore in-memory states.
    bool can_save_state = false;

    /// Whether saved states can be serialized and imported.
    bool can_export_state = false;
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
        case variable_type::float32: return "float32";
        case variable_type::int8: return "int8";
        case variable_type::uint8: return "uint8";
        case variable_type::int16: return "int16";
        case variable_type::uint16: return "uint16";
        case variable_type::uint32: return "uint32";
        case variable_type::int64: return "int64";
        case variable_type::uint64: return "uint64";
        case variable_type::binary: return "binary";
        case variable_type::clock: return "clock";
        default: return "NULL";
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
        case variable_causality::structural_parameter: return "structural_parameter";
        case variable_causality::independent: return "independent";
        default: return "NULL";
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
        default: return "NULL";
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
 *  Identifies a variable by exact type and value reference.
 *
 *  FMI value references may overlap between exact types, so both fields are
 *  required for unambiguous generic value access.
 */
struct typed_value_reference
{
    /// Exact FMI type of the referenced variable.
    variable_type type = variable_type::real;

    /// FMI value reference within that exact type.
    value_reference reference = 0;
};


inline bool operator==(
    const typed_value_reference& a,
    const typed_value_reference& b) noexcept
{
    return a.type == b.type && a.reference == b.reference;
}


inline bool operator!=(
    const typed_value_reference& a,
    const typed_value_reference& b) noexcept
{
    return !(a == b);
}


/**
 *  A distinct representation of an FMI Enumeration value.
 */
struct enumeration_value
{
    /// Numeric value of the FMI Enumeration item.
    std::int64_t value = 0;
};


inline bool operator==(enumeration_value a, enumeration_value b) noexcept
{
    return a.value == b.value;
}


inline bool operator!=(enumeration_value a, enumeration_value b) noexcept
{
    return !(a == b);
}


inline std::ostream& operator<<(std::ostream& stream, enumeration_value value)
{
    return stream << value.value;
}


/**
 *  An owning FMI Binary value.
 */
struct binary_value
{
    /// Owning byte sequence returned by or supplied to the FMU.
    std::vector<std::uint8_t> data;
};


inline bool operator==(const binary_value& a, const binary_value& b) noexcept
{
    return a.data == b.data;
}


inline bool operator!=(const binary_value& a, const binary_value& b) noexcept
{
    return !(a == b);
}


std::ostream& operator<<(std::ostream& stream, const binary_value& value);


/**
 *  An algebraic type that can hold a scalar value of one of the supported
 *  variable types.
 */
using scalar_value = std::variant<
    double,
    int,
    bool,
    std::string,
    float,
    std::int8_t,
    std::uint8_t,
    std::int16_t,
    std::uint16_t,
    std::uint32_t,
    std::int64_t,
    std::uint64_t,
    enumeration_value,
    binary_value>;


/**
 *  An algebraic type that can hold a (possibly) non-owning, read-only view
 *  of a scalar value of one of the supported variable types.
 *
 *  In practice, it's only for strings that this type is a view; for all
 *  other types it holds a copy.
 */
using scalar_value_view = std::variant<
    double,
    int,
    bool,
    std::string_view,
    float,
    std::int8_t,
    std::uint8_t,
    std::int16_t,
    std::uint16_t,
    std::uint32_t,
    std::int64_t,
    std::uint64_t,
    enumeration_value,
    const binary_value*>;


/// Returns the exact variable type represented by `value`.
variable_type type_of(const scalar_value& value) noexcept;


/**
 *  A homogeneous, flat sequence of values of exact type `Type`.
 */
template<typename T, variable_type Type>
struct typed_variable_values
{
    using value_type = T;
    static constexpr variable_type type = Type;

    /// Flat, row-major scalar or array elements in FMI value order.
    std::vector<T> values;
};


template<typename T, variable_type Type>
inline bool operator==(
    const typed_variable_values<T, Type>& a,
    const typed_variable_values<T, Type>& b)
{
    return a.values == b.values;
}


template<typename T, variable_type Type>
inline bool operator!=(
    const typed_variable_values<T, Type>& a,
    const typed_variable_values<T, Type>& b)
{
    return !(a == b);
}


using real_values = typed_variable_values<double, variable_type::real>;
using integer_values = typed_variable_values<int, variable_type::integer>;
using boolean_values = typed_variable_values<bool, variable_type::boolean>;
using string_values = typed_variable_values<std::string, variable_type::string>;
using enumeration_values =
    typed_variable_values<enumeration_value, variable_type::enumeration>;
using float32_values = typed_variable_values<float, variable_type::float32>;
using int8_values = typed_variable_values<std::int8_t, variable_type::int8>;
using uint8_values = typed_variable_values<std::uint8_t, variable_type::uint8>;
using int16_values = typed_variable_values<std::int16_t, variable_type::int16>;
using uint16_values =
    typed_variable_values<std::uint16_t, variable_type::uint16>;
using uint32_values =
    typed_variable_values<std::uint32_t, variable_type::uint32>;
using int64_values = typed_variable_values<std::int64_t, variable_type::int64>;
using uint64_values =
    typed_variable_values<std::uint64_t, variable_type::uint64>;
using binary_values = typed_variable_values<binary_value, variable_type::binary>;


using variable_value_storage = std::variant<
    real_values,
    integer_values,
    boolean_values,
    string_values,
    enumeration_values,
    float32_values,
    int8_values,
    uint8_values,
    int16_values,
    uint16_values,
    uint32_values,
    int64_values,
    uint64_values,
    binary_values>;


/**
 *  An owning value of one scalar or array variable.
 *
 *  `dimensions` is empty for a scalar. Array elements are stored flat in FMI
 *  row-major order.
 */
class variable_value
{
public:
    /// Creates an empty value with the default exact type.
    variable_value();

    /**
     *  Creates an owning scalar or array value.
     *
     *  An empty dimension list represents one scalar element. For arrays,
     *  `values` must contain exactly `flat_size(dimensions)` elements.
     */
    explicit variable_value(
        variable_value_storage values,
        std::vector<std::uint64_t> dimensions = {});

    /// Returns the exact FMI type stored in this value.
    variable_type type() const noexcept;

    /// Returns true when the value represents one scalar element.
    bool is_scalar() const noexcept;

    /// Returns the number of flat elements stored in the value.
    std::size_t size() const noexcept;

    /// Returns array dimensions in FMI declaration order.
    const std::vector<std::uint64_t>& dimensions() const noexcept;

    /// Returns the owning type-specific value storage.
    const variable_value_storage& values() const noexcept;

private:
    std::vector<std::uint64_t> dimensions_;
    variable_value_storage values_;
};


bool operator==(const variable_value& a, const variable_value& b);
bool operator!=(const variable_value& a, const variable_value& b);


/**
 *  Calculates the number of flat values described by `dimensions`.
 *
 *  An empty dimension list represents a scalar and therefore has size one.
 *  Throws `std::overflow_error` if the product does not fit in `std::size_t`.
 */
std::size_t flat_size(const std::vector<std::uint64_t>& dimensions);


/// The FMI `initial` attribute.
enum class variable_initial
{
    /// The start value is an exact initial condition.
    exact,
    /// The start value is an approximation.
    approximate,
    /// The FMU calculates the initial value.
    calculated
};


/// A fixed array dimension.
struct fixed_dimension
{
    /// Number of elements in this dimension.
    std::uint64_t size = 0;
};


/// An array dimension whose size is given by a structural parameter.
struct variable_dimension
{
    /// FMI value reference of the structural parameter.
    value_reference structural_parameter = 0;
};


using variable_dimension_description =
    std::variant<fixed_dimension, variable_dimension>;


/// An alias of a model variable.
struct variable_alias
{
    /// Alternate model-variable name.
    std::string name;

    /// Optional description of the alias.
    std::optional<std::string> description;

    /// Optional display unit override for the alias.
    std::optional<std::string> display_unit;
};


/// FMI Clock interval variability.
enum class clock_interval_variability
{
    /// No scheduling information is available.
    unknown,
    /// The interval is constant for all activations.
    constant,
    /// The interval is fixed for the current run.
    fixed,
    /// The host may tune the interval.
    tunable,
    /// The FMU may report a new interval after activation.
    changing,
    /// The interval is a countdown value.
    countdown,
    /// Activation is driven by an explicit event.
    triggered
};


/// Metadata specific to an FMI Clock variable.
struct clock_description
{
    /// How the interval is determined.
    clock_interval_variability interval_variability =
        clock_interval_variability::triggered;

    /// Whether the Clock can be deactivated in the current mode.
    bool can_be_deactivated = false;

    /// Optional FMI scheduling priority.
    std::optional<std::uint32_t> priority;

    /// Whether a fractional interval/shift representation is declared.
    bool supports_fraction = false;

    /// Decimal interval declared by the FMU.
    std::optional<double> interval_decimal;

    /// Decimal shift declared by the FMU.
    std::optional<double> shift_decimal;

    /// Fractional interval counter.
    std::optional<std::uint64_t> interval_counter;

    /// Fractional interval resolution.
    std::optional<std::uint64_t> resolution;

    /// Fractional shift counter.
    std::optional<std::uint64_t> shift_counter;
};


/// One item in an Enumeration type definition.
struct enumeration_item
{
    /// Symbolic Enumeration item name.
    std::string name;

    /// Numeric Enumeration item value.
    std::int64_t value = 0;

    /// Human-readable item description.
    std::string description;
};


/// An FMI declared type definition.
struct variable_type_definition
{
    /// Declared type name referenced by model variables.
    std::string name;

    /// Human-readable declared-type description.
    std::string description;

    /// Exact FMI type represented by the declaration.
    variable_type type = variable_type::real;

    /// Optional quantity associated with the declared type.
    std::optional<std::string> quantity;

    /// Optional unit name.
    std::optional<std::string> unit;

    /// Optional display-unit name.
    std::optional<std::string> display_unit;

    /// Optional lower bound.
    std::optional<scalar_value> min;

    /// Optional upper bound.
    std::optional<scalar_value> max;

    /// Optional nominal value.
    std::optional<scalar_value> nominal;

    /// Whether the quantity is relative to another quantity.
    std::optional<bool> relative_quantity;

    /// Whether the value is unbounded.
    std::optional<bool> unbounded;

    /// Enumeration items, when this is an Enumeration type.
    std::vector<enumeration_item> enumeration_items;

    /// MIME type, when this is a Binary type.
    std::optional<std::string> mime_type;

    /// Maximum Binary payload size.
    std::optional<std::uint64_t> max_size;

    /// Clock metadata, when this is a Clock type.
    std::optional<clock_description> clock;
};


/// An FMI display-unit definition.
struct display_unit_definition
{
    /// Display-unit name.
    std::string name;

    /// Multiplicative conversion factor.
    double factor = 1.0;

    /// Additive conversion offset.
    double offset = 0.0;

    /// Whether the conversion uses the inverse relationship.
    bool inverse = false;
};


/// An FMI unit definition.
struct unit_definition
{
    /// Unit name.
    std::string name;

    /// SI base-unit exponents.
    int kilogram = 0;
    int metre = 0;
    int second = 0;
    int ampere = 0;
    int kelvin = 0;
    int mole = 0;
    int candela = 0;
    int radian = 0;

    /// Base conversion factor and offset.
    double factor = 1.0;
    double offset = 0.0;

    /// Named display-unit conversions for this unit.
    std::vector<display_unit_definition> display_units;
};


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

    /// The variable's start value. Arrays use `array_start` below.
    std::optional<scalar_value> start;

    /// Whether the variable may be accessed during FMI 3 Intermediate Update Mode.
    bool intermediate_update = false;

    /// A human-readable description.
    std::string description;

    /// The FMI `initial` attribute, when present.
    std::optional<variable_initial> initial;

    /// The name of the variable's declared type, when present.
    std::optional<std::string> declared_type;

    /// Ordered array dimensions. Empty for a scalar.
    std::vector<variable_dimension_description> dimensions;

    /// The array start value. Scalar starts continue to use `start`.
    std::optional<variable_value> array_start;

    /// Quantity name, when declared.
    std::optional<std::string> quantity;

    /// Unit name, when declared.
    std::optional<std::string> unit;

    /// Display unit name, when declared.
    std::optional<std::string> display_unit;

    /// Optional lower bound.
    std::optional<scalar_value> min;

    /// Optional upper bound.
    std::optional<scalar_value> max;

    /// Optional nominal value.
    std::optional<scalar_value> nominal;

    /// Whether the quantity is relative.
    std::optional<bool> relative_quantity;

    /// Whether the value is unbounded.
    std::optional<bool> unbounded;

    /// Alternate names for the variable.
    std::vector<variable_alias> aliases;

    /// Previous-variable reference used by FMI derivative relationships.
    std::optional<value_reference> previous;

    /// Derivative-variable reference, when declared.
    std::optional<value_reference> derivative;

    /// Whether the variable may be reinitialized.
    std::optional<bool> reinit;

    /// Whether multiple writes at one time instant are supported.
    std::optional<bool> can_handle_multiple_set_per_time_instant;

    /// Clocks associated with this variable.
    std::vector<value_reference> clocks;

    /// MIME type, when this is a Binary variable.
    std::optional<std::string> mime_type;

    /// Maximum Binary payload size.
    std::optional<std::uint64_t> max_size;

    /// Clock-specific metadata.
    std::optional<clock_description> clock;
};


/// A description of a model.
struct model_description
{
    /// The model name.
    std::string name;

    /// A universally unique identifier (UUID) for the model.
    /// This is populated for FMI 1.0 and FMI 2.0 models.
    std::string uuid;

    /// A human-readable description of the model.
    std::string description;

    /// Author information.
    std::string author;

    /// Version information.
    std::string version;

    /// Variable descriptions.
    std::vector<variable_description> variables;

    /// Simulator capabilities
    simulator_capabilities capabilities;

    /// The FMI 3.0 instantiation token for the model.
    /// FMI 3.0 does not define a UUID attribute.
    std::string instantiation_token;

    /// FMI declared type definitions.
    std::vector<variable_type_definition> type_definitions;

    /// FMI unit definitions.
    std::vector<unit_definition> unit_definitions;
};

/// Getter for returning a variable description.
std::optional<variable_description> find_variable(const model_description& description, const std::string& variable_name);

/**
 *  Returns the fixed dimensions of `variable`.
 *
 *  Returns an empty vector for a scalar and `std::nullopt` if at least one
 *  dimension depends on a structural parameter.
 */
std::optional<std::vector<std::uint64_t>>
fixed_shape(const variable_description& variable);

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


/**
 *  Detailed information about a completed co-simulation step for one
 *  simulator.
 *
 *  `last_successful_time` is the endpoint actually reached by the simulator.
 *  It is never beyond the requested endpoint. A terminated step is otherwise
 *  successful, but no subsequent steps may be requested.
 */
struct step_result_info
{
    /// Coarse legacy result for callers that do not need FMI control flags.
    step_result result = step_result::complete;

    /// Endpoint actually reached by the simulator.
    time_point last_successful_time{};

    /// The FMU returned before the requested communication point.
    bool early_return = false;

    /// Event Mode processing is required before the next step.
    bool event_handling_needed = false;

    /// The FMU requested termination; no later step is valid.
    bool termination_requested = false;

    /// The failed step may be retried with a shorter interval.
    bool retryable = false;
};

using step_outcome = step_result_info;


} // namespace cosim
#endif // COSIM_MODEL_HPP
