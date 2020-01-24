/**
 *  \file
 *  Types that describe functions.
 */
#ifndef CSE_FUNCTION_DESCRIPTION_HPP
#define CSE_FUNCTION_DESCRIPTION_HPP

#include <cse/model.hpp>

#include <optional>
#include <string>
#include <variant>
#include <vector>


namespace cse
{


/// Function parameter types
enum class function_parameter_type
{
    /// A real number
    real,

    /// An integer
    integer,

    /// A variable type
    type
};


/// Type that holds the value of a function parameter.
using function_parameter_value = std::variant<double, int, variable_type>;


/// A description of a function parameter.
struct function_parameter_description
{
    std::string name;
    function_parameter_type type;
    function_parameter_value default_value;
    std::optional<function_parameter_value> min_value;
    std::optional<function_parameter_value> max_value;
};


/**
 *  A placeholder that may be used in certain fields in `function_io_description`
 *  and `function_io_group_description` to represent an as-of-yet unspecified
 *  parameter value.
 */
struct function_parameter_placeholder
{
    int parameter_index;
};


/// A description of one of a function's input or output variables.
struct function_io_description
{
    std::string name;
    std::variant<variable_type, function_parameter_placeholder> type;
    variable_causality causality;
    std::variant<int, function_parameter_placeholder> count;
};


/// A description of one of a function's groups of input and output variables.
struct function_io_group_description
{
    std::string name;
    std::variant<int, function_parameter_placeholder> count;
    std::vector<function_io_description> ios;
};


/// A description of a function type.
struct function_type_description
{
    std::string name;
    std::vector<function_parameter_description> parameters;
    std::vector<function_io_group_description> io_groups;
};


/// Uniquely identifies a particular input or output variable.
struct function_io_reference
{
    /// The group index
    int group;

    /// The particular instance of the group
    int group_instance;

    /// The index of the variable within the group
    int io;

    /// The particular instance of the variable
    int io_instance;
};


/// Equality operator for `function_io_reference`.
inline bool operator==(
    const function_io_reference& a,
    const function_io_reference& b) noexcept
{
    return a.group == b.group &&
        a.group_instance == b.group_instance &&
        a.io == b.io &&
        a.io_instance == b.io_instance;
}


/// Inequality operator for `function_io_reference`.
inline bool operator!=(
    const function_io_reference& a,
    const function_io_reference& b) noexcept
{
    return !operator==(a, b);
}


} // namespace cse
#endif // header guard
