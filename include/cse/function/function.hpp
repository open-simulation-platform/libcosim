#ifndef CSE_FUNCTION_FUNCTION_HPP
#define CSE_FUNCTION_FUNCTION_HPP

#include <cse/model.hpp>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>


namespace cse
{


/// Function parameter types
enum class function_parameter_type
{
    /// An integer
    integer,

    /// A variable type
    type
};


/// Type that holds the value of a function parameter.
using function_parameter_value = std::variant<int, variable_type>;


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
 *  An placeholder that may be used in certain fields in `function_io_description`
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


/// A function instance.
class function
{
public:
    virtual function_type_description type_description() const = 0;

    virtual void set_parameter(int index, function_parameter_value value) = 0;

    virtual void initialize() = 0;

    virtual void set_io(int groupIndex, int ioIndex, scalar_value_view value) = 0;

    virtual scalar_value_view get_io(int groupIndex, int ioIndex) const = 0;

    virtual void calculate() = 0;
};


/// A function type.
class function_type
{
public:
    virtual function_type_description description() const = 0;
    virtual std::unique_ptr<function> instantiate(std::string_view name) = 0;
};


} // namespace cse
#endif // header guard
