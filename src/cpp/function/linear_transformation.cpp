#include "cse/function/linear_transformation.hpp"

#include "cse/function/utility.hpp"

#include <stdexcept>


using namespace std::literals;


namespace cse
{
namespace
{

enum linear_transformation_parameters
{
    linear_transformation_offset,
    linear_transformation_factor,
};

function_type_description linear_transformation_description()
{
    return {
        "LinearTransformation"s,
        {
            // parameters (in the same order as linear_transformation_parameters enum!)
            function_parameter_description{
                "offset"s, // name
                function_parameter_type::real, // type
                0.0, // default_value
                {}, // min_value
                {} // max_value
            },
            function_parameter_description{
                "factor"s, // name
                function_parameter_type::real, // type
                1.0, // default_value
                {}, // min_value
                {} // max_value
            },
        },
        {
            // io_groups
            function_io_group_description{
                "in"s, // name
                1, // count
                {
                    // ios
                    function_io_description{
                        ""s, // name (inherited from group)
                        variable_type::real, // type
                        variable_causality::input, // causality
                        1, // count
                    },
                }},
            function_io_group_description{
                "out"s, // name
                1, // count
                {
                    // ios
                    function_io_description{
                        ""s, // name = (inherited from group)
                        variable_type::real, // type
                        variable_causality::output, // causality
                        1, // count
                    },
                }},
        }};
}

} // namespace


// =============================================================================
// linear_transformation_function
// =============================================================================

linear_transformation_function::linear_transformation_function(double offset, double factor)
    : offset_(offset)
    , factor_(factor)
{
}

function_type_description linear_transformation_function::description() const
{
    return linear_transformation_description();
}

void linear_transformation_function::set_real_io(
    const function_io_reference& reference,
    double value)
{
    if (reference == function_io_reference{0, 0, 0, 0}) {
        input_ = value;
    } else {
        throw std::out_of_range("Invalid function variable reference");
    }
}

void linear_transformation_function::set_integer_io(
    const function_io_reference&, int)
{
    throw std::out_of_range("Invalid function variable reference");
}

double linear_transformation_function::get_real_io(
    const function_io_reference& reference) const
{
    if (reference == function_io_reference{0, 0, 0, 0}) {
        return input_;
    } else if (reference == function_io_reference{1, 0, 0, 0}) {
        return output_;
    } else {
        throw std::out_of_range("Invalid function variable reference");
    }
}

int linear_transformation_function::get_integer_io(
    const function_io_reference&) const
{
    throw std::out_of_range("Invalid function variable reference");
}

void linear_transformation_function::calculate()
{
    output_ = offset_ + factor_ * input_;
}


// =============================================================================
// linear_transformation_function_type
// =============================================================================

function_type_description linear_transformation_function_type::description() const
{
    return linear_transformation_description();
}

std::unique_ptr<function> linear_transformation_function_type::instantiate(
    const std::unordered_map<int, function_parameter_value> parameters)
{
    const auto descr = description();
    const auto offset =
        get_function_parameter<double>(descr, parameters, linear_transformation_offset);
    const auto factor =
        get_function_parameter<double>(descr, parameters, linear_transformation_factor);

    return std::make_unique<linear_transformation_function>(offset, factor);
}

} // namespace cse
