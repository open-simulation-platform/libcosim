#include "cse/function/vector_sum.hpp"

#include "cse/function/utility.hpp"


using namespace std::literals;

namespace cse
{
enum vector_sum_parameters
{
    vector_sum_inputCount = 0,
    vector_sum_numericType = 1,
    vector_sum_dimension = 2,
};


namespace detail
{
function_type_description vector_sum_description(
    std::variant<int, function_parameter_placeholder> inputCount,
    std::variant<variable_type, function_parameter_placeholder> numericType,
    std::variant<int, function_parameter_placeholder> dimension)
{
    return {
        "VectorSum",
        {
            // parameters (in the same order as vector_sum_parameters enum!)
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
        },
        {
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
        }};
}
} // namespace detail


function_type_description vector_sum_function_type::description() const
{
    return detail::vector_sum_description(
        function_parameter_placeholder{vector_sum_inputCount},
        function_parameter_placeholder{vector_sum_numericType},
        function_parameter_placeholder{vector_sum_dimension});
}


std::unique_ptr<function> vector_sum_function_type::instantiate(
    const std::unordered_map<int, function_parameter_value> parameters)
{
    const auto descr = description();
    const auto inputCount =
        get_function_parameter<int>(descr, parameters, vector_sum_inputCount);
    const auto numericType =
        get_function_parameter<variable_type>(descr, parameters, vector_sum_numericType);
    const auto dimension =
        get_function_parameter<int>(descr, parameters, vector_sum_dimension);

    if (numericType == variable_type::real) {
        return std::make_unique<vector_sum_function<double>>(inputCount, dimension);
    } else if (numericType == variable_type::integer) {
        return std::make_unique<vector_sum_function<int>>(inputCount, dimension);
    } else {
        throw std::domain_error("Parameter 'numericType' must be 'real' or 'integer'");
    }
}


} // namespace cse
