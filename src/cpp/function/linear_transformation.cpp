#include "cse/function/linear_transformation.hpp"

#include "cse/error.hpp"

#include <gsl/span>

#include <cassert>
#include <stdexcept>


using namespace std::literals;


namespace cse
{

enum linear_transformation_parameters
{
    linear_transformation_offset = 0,
    linear_transformation_factor = 1,
};


function_type_description linear_transformation_function::type_description() const
{
    return {
        "VectorSum",
        {
            // parameters (in the same order as linear_transformation_parameter enum!)
            function_parameter_description{
                "offset", // name
                function_parameter_type::real, // type
                0.0, // default_value
                {}, // min_value
                {} // max_value
            },
            function_parameter_description{
                "factor", // name
                function_parameter_type::real, // type
                1.0, // default_value
                {}, // min_value
                {} // max_value
            },
        },
        {
            // io_groups
            function_io_group_description{
                "in", // name
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
                "out", // name
                1, // count
                {
                    // ios
                    function_io_description{
                        ""s, // name (inherited from group)
                        variable_type::real, // type
                        variable_causality::output, // causality
                        1, // count
                    },
                }},
        }};
}


void linear_transformation_function::set_parameter(int index, function_parameter_value value)
{
    switch (index) {
        case linear_transformation_offset:
            offset_ = std::get<double>(value);
            break;
        case linear_transformation_factor:
            factor_ = std::get<double>(value);
            break;
    };
}


void linear_transformation_function::initialize()
{
    // no action needed
}


void linear_transformation_function::set_io(int groupIndex, int ioIndex, scalar_value_view value)
{
    if (groupIndex == 0 && ioIndex == 0) {
        input_ = std::get<double>(value);
    } else {
        throw std::out_of_range("Invalid variable/group index");
    }
}


scalar_value_view linear_transformation_function::get_io(int groupIndex, int ioIndex) const
{
    if (groupIndex == 0 && ioIndex == 0) {
        return input_;
    } else if (groupIndex == 1 && ioIndex == 0) {
        return output_;
    } else {
        throw std::out_of_range("Invalid variable/group index");
    }
}


void linear_transformation_function::calculate()
{
    output_ = offset_ + factor_ * input_;
}

} // namespace cse
