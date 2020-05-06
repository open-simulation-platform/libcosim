#include "cse/function/vector_sum.hpp"

#include "cse/function/utility.hpp"


using namespace std::literals;

namespace cosim
{


namespace detail
{
function_type_description vector_sum_description(
    std::variant<int, function_parameter_placeholder> inputCount,
    std::variant<variable_type, function_parameter_placeholder> numericType,
    std::variant<int, function_parameter_placeholder> dimension)
{
    function_type_description f;
    f.parameters = {
        // parameters (in the same order as the enum in vector_sum_function_type!)
        function_parameter_description{
            "inputCount"s, // name
            function_parameter_type::integer, // type
            1, // default_value
            1, // min_value
            {} // max_value
        },
        function_parameter_description{
            "numericType"s, // name
            function_parameter_type::type, // type
            variable_type::real, // default_value
            {}, // min_value
            {} // max_value
        },
        function_parameter_description{
            "dimension"s, // name
            function_parameter_type::integer, // type
            1, // default_value
            1, // min_value
            {} // max_value
        },
    };
    f.io_groups = {
        // io_groups
        function_io_group_description{
            "in"s, // name
            inputCount, // count
            {
                // ios
                function_io_description{
                    ""s, // name (inherited from group)
                    numericType, // type
                    variable_causality::input, // causality
                    dimension, // count
                },
            }},
        function_io_group_description{
            "out"s, // name
            1, // count
            {
                // ios
                function_io_description{
                    ""s, // name = (inherited from group)
                    numericType, // type
                    variable_causality::output, // causality
                    dimension, // count
                },
            }},
    };
    return f;
}
} // namespace detail


function_type_description vector_sum_function_type::description() const
{
    return detail::vector_sum_description(
        function_parameter_placeholder{vector_sum_function_type::inputCount_parameter_index},
        function_parameter_placeholder{vector_sum_function_type::numericType_parameter_index},
        function_parameter_placeholder{vector_sum_function_type::dimension_parameter_index});
}


std::unique_ptr<function> vector_sum_function_type::instantiate(
    const function_parameter_value_map& parameters)
{
    const auto descr = description();
    const auto inputCount = get_function_parameter<int>(
        descr, parameters, vector_sum_function_type::inputCount_parameter_index);
    const auto numericType = get_function_parameter<variable_type>(
        descr, parameters, vector_sum_function_type::numericType_parameter_index);
    const auto dimension = get_function_parameter<int>(
        descr, parameters, vector_sum_function_type::dimension_parameter_index);

    if (numericType == variable_type::real) {
        return std::make_unique<vector_sum_function<double>>(inputCount, dimension);
    } else if (numericType == variable_type::integer) {
        return std::make_unique<vector_sum_function<int>>(inputCount, dimension);
    } else {
        throw std::domain_error("Parameter 'numericType' must be 'real' or 'integer'");
    }
}


} // namespace cse
