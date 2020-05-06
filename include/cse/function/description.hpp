/**
 *  \file
 *  Types that describe functions.
 */
#ifndef CSE_FUNCTION_DESCRIPTION_HPP
#define CSE_FUNCTION_DESCRIPTION_HPP

#include <cse/model.hpp>

#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>


namespace cosim
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


/**
 *  Type that holds the value of a function parameter.
 *
 *  This is a variant type that can hold values of the types enumerated by
 *  `function_parameter_type`.
 */
using function_parameter_value = std::variant<double, int, variable_type>;


/**
 *  An associative container for function parameter values.
 *
 *  The container maps parameter indexes, defined as the positions of the
 *  parameter descriptions in some `function_type_description::parameters`
 *  list, to values.
 */
using function_parameter_value_map =
    std::unordered_map<int, function_parameter_value>;


/// A description of a function parameter.
struct function_parameter_description
{
    /// The parameter name.
    std::string name;

    /// The parameter type.
    function_parameter_type type;

    /**
     *  A default value for the parameter.
     *
     *  This is what the function will use if no other value is passed
     *  to `function_type::instantiate()`.
     */
    function_parameter_value default_value;

    /**
     *  An optional minimum value for the parameter.
     *
     *  If specified, this is the smallest value allowed for the parameter.
     *  This only makes sense for numeric parameters and should be ignored
     *  for all others.
     */
    std::optional<function_parameter_value> min_value;

    /**
     *  An optional maximum value for the parameter.
     *
     *  If specified, this is the largest value allowed for the parameter.
     *  This only makes sense for numeric parameters and should be ignored
     *  for all others.
     */
    std::optional<function_parameter_value> max_value;
};


/**
 *  A placeholder that may be used in certain fields in `function_io_description`
 *  and `function_io_group_description` to indicate that the field value depends
 *  on the value of some parameter.
 *
 *  This only makes sense in the context of function *type* descriptions (as
 *  returned by `function_type::description()`), where some aspects of a
 *  function such as the number and types of variables may be configurable
 *  through parameters.
 *
 *  Function *instance* description (as returned by `function::description()`)
 *  can not contain such placeholders, since all parameters obtain a fixed
 *  value upon instantiation.
 *
 *  \see `function_io_description`
 *  \see `function_io_group_description`
 */
struct function_parameter_placeholder
{
    /**
     *  The index of the parameter for whose value this object is a placeholder.
     *
     *  This index refers to the parameter's position in the
     *  `function_type_description::parameters` list.
     */
    int parameter_index;
};


/// A description of one of a function's input or output variables.
struct function_io_description
{
    /**
     *  The variable name.
     *
     *  In the special case where the variable is the only one in a group,
     *  the name may be left empty, since the variable can be uniquely
     *  referred to via the name of its group.
     */
    std::string name;

    /**
     *  The variable type.
     *
     *
     *  If the variable type can be specified by client code by means of a
     *  parameter, this field may contain a `function_parameter_placeholder`
     *  that refers to the parameter in question.  This enables the creation
     *  of generic functions that can be applied to different (user-specified)
     *  types.
     */
    std::variant<variable_type, function_parameter_placeholder> type;

    /**
     *  The variable causality.
     *
     *  The only allowed causalities for function variables are
     *  `variable_causality::input` and `variable_causality::output`.
     */
    variable_causality causality;

    /**
     *  The number of instances of this variable.
     *
     *  Each instance effectively acts as a separate variable, so the whole
     *  set can simply be viewed as an array of size `count`.
     *
     *  If the instance count can be specified by client code by means of a
     *  parameter, this field may contain a `function_parameter_placeholder`
     *  that refers to the parameter in question.  An example use case might
     *  be to support vector variables with user-defined dimensions.
     */
    std::variant<int, function_parameter_placeholder> count;
};


/// A description of one of a function's groups of input and output variables.
struct function_io_group_description
{
    /// The variable group name.
    std::string name;

    /**
     *  The number of instances of this group.
     *
     *  Each instance effectively acts as a separate set of variables.
     *
     *  If the instance count can be specified by client code by means of a
     *  parameter, this field may contain a `function_parameter_placeholder`
     *  that refers to the parameter in question.  A typical use case for this
     *  would be to support a user-defined number of structured inputs, for
     *  example multiple input vectors.
     *
     *  (Expanding on this example, to support a user-defined number of vectors
     *  of user-defined dimension, use `function_parameter_placeholder` objects
     *  for both the group instance count and the instance count of a variable
     *  in the group.  See `function_io_description::count` for more info about
     *  the latter.)
     */
    std::variant<int, function_parameter_placeholder> count;

    /// The variables in this group.
    std::vector<function_io_description> ios;
};


/**
 *  A description of a function.
 *
 *  This structure can be used to describe either a function type or a function
 *  instance.
 *
 *  In the former case, it will be embedded in a `function_type_description`
 *  (which derives from `function_description`).  The variable descriptions
 *  may then depend on parameters, so some of its subfields may contain
 *  values of type `function_parameter_placeholder`.  See
 *  `function_io_group_description` and `function_io_description` for
 *  information about which fields these are.
 *
 *  On the other hand, if the structure is used to describe a function
 *  *instance*, it may not contain any parameter placeholders; all subfields
 *  must have concrete values.
 */
struct function_description
{
    /// Information about the function's variable groups.
    std::vector<function_io_group_description> io_groups;
};


/// A description of a function type.
struct function_type_description : public function_description
{
    /**
     *  The function's parameters.
     *
     *  Parameters are values which must be specified upon instantiation of
     *  a `function` with `function_type::instantiate()`.  They typically
     *  determine properties of the function which cannot change during a
     *  simulation, such as the number and types of input/output variables.
     */
    std::vector<function_parameter_description> parameters;
};


/**
 *  Uniquely identifies a particular input/output variable of a function
 *  instance.
 *
 *  This is typically used with functions that read or write variable values,
 *  such as `function::set_real()` and its siblings.
 *
 *  The meaning of the various fields is best illustrated with an example:
 *
 *  Let's say we have a "3D vector sum" function with two 3D input vectors
 *  and one 3D output vector.  Let's furthermore say that we implement
 *  these as two variable groups called `input` and `output`, listed in that
 *  order in `function_description::io_groups`.
 *
 *  We would then have two instances of the `input` group and one instance
 *  of the `output` group.  That is, their `function_io_group_description::count`
 *  values would be 2 and 1, respectively.  Each group would contain one
 *  variable with 3 instances (i.e., `function_io_description::count = 3`).
 *
 *  Now, to refer to the third component of the second input vector, we'd use:
 *
 *                            .-----------> first group, i.e., the input vectors
 *                            |  .--------> second instance of that group, i.e., the second input vector
 *                            |  |  .-----> first (and only) variable in the group
 *                            |  |  |  .--> third instance of that variable, i.e., the third component of the vector
 *      function_io_reference{0, 1, 0, 2}
 *
 *  Similarly, to refer to the second component of the first (and only) output
 *  vector, we'd use `function_io_reference{1, 0, 0, 1}`.
 */
struct function_io_reference
{
    /**
     *  The group index.
     *
     *  This number corresponds to the group's position in the
     *  `function_description::io_groups` list for the function in
     *  question.
     */
    int group;

    /**
     *  The particular instance of the group.
     *
     *  This number must be in the range `[0,n)`, where `n` is the value of
     *  `function_io_group_description::count` for the group in question.
     */
    int group_instance;

    /**
     *  The index of the variable within the group.
     *
     *  This number corresponds to the variable's position in the
     *  `function_io_group_description::ios` list for the group in
     *  question.
     */
    int io;

    /**
     *  The particular instance of the variable
     *
     *  This number must be in the range `[0,n)`, where `n` is the value of
     *  `function_io_description::count` for the variable in question.
     */
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
