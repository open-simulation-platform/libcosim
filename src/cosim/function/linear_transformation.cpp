/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/function/linear_transformation.hpp"

#include "cosim/function/description.hpp"
#include "cosim/function/function.hpp"
#include "cosim/function/utility.hpp"
#include "cosim/model_description.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>


namespace cosim
{
namespace
{

function_type_description linear_transformation_description()
{
    function_type_description description;
    description.parameters = {
        // parameters (in the same order as linear_transformation_parameters enum!)
        function_parameter_description{
            std::string{"offset"}, // name
            function_parameter_type::real, // type
            0.0, // default_value
            {}, // min_value
            {}, // max_value
        },
        function_parameter_description{
            std::string{"factor"}, // name
            function_parameter_type::real, // type
            1.0, // default_value
            {}, // min_value
            {}, // max_value
        },
    };
    description.io_groups = {
        // io_groups
        function_io_group_description{
            std::string{"in"}, // name
            1, // count
            {
                // ios
                function_io_description{
                    std::string{}, // name (inherited from group)
                    variable_type::real, // type
                    variable_causality::input, // causality
                    1, // count
                    {}, // dimensions
                },
            },
        },
        function_io_group_description{
            std::string{"out"}, // name
            1, // count
            {
                // ios
                function_io_description{
                    std::string{}, // name = (inherited from group)
                    variable_type::real, // type
                    variable_causality::output, // causality
                    1, // count
                    {}, // dimensions
                },
            },
        },
    };
    return description;
}

} // namespace


// =============================================================================
// linear_transformation_function
// =============================================================================

[[noreturn]] void throw_bad_io_ref()
{
    throw std::out_of_range("Invalid function variable reference");
}


linear_transformation_function::linear_transformation_function(double offset, double factor) // NOLINT(bugprone-easily-swappable-parameters): Preserve the public constructor signature.
    : offset_(offset)
    , factor_(factor)
{
}

function_description linear_transformation_function::description() const
{
    return linear_transformation_description(); // NOLINT(cppcoreguidelines-slicing): Return the function instance description, not its type parameters.
}

void linear_transformation_function::set_real(
    const function_io_reference& reference,
    double value)
{
    if (reference == in_io_reference) {
        input_ = value;
    } else {
        throw_bad_io_ref();
    }
}

void linear_transformation_function::set_integer(
    const function_io_reference& /*reference*/, int /*value*/)
{
    throw_bad_io_ref();
}

void linear_transformation_function::set_boolean(
    const function_io_reference& /*reference*/, bool /*value*/)
{
    throw_bad_io_ref();
}

void linear_transformation_function::set_string(
    const function_io_reference& /*reference*/, std::string_view /*value*/)
{
    throw_bad_io_ref();
}

double linear_transformation_function::get_real(
    const function_io_reference& reference) const
{
    if (reference == in_io_reference) {
        return input_;
    } else if (reference == out_io_reference) {
        return output_;
    } else {
        throw_bad_io_ref();
    }
}

int linear_transformation_function::get_integer(
    const function_io_reference& /*reference*/) const
{
    throw_bad_io_ref();
}

bool linear_transformation_function::get_boolean(
    const function_io_reference& /*reference*/) const
{
    throw_bad_io_ref();
}

std::string_view linear_transformation_function::get_string(
    const function_io_reference& /*reference*/) const
{
    throw_bad_io_ref();
}

void linear_transformation_function::calculate()
{
    output_ = offset_ + (factor_ * input_);
}


// =============================================================================
// linear_transformation_function_type
// =============================================================================

function_type_description linear_transformation_function_type::description() const
{
    return linear_transformation_description();
}

std::unique_ptr<function> linear_transformation_function_type::instantiate(
    const function_parameter_value_map& parameters)
{
    const auto descr = description();
    const auto offset =
        get_function_parameter<double>(descr, parameters, offset_parameter_index);
    const auto factor =
        get_function_parameter<double>(descr, parameters, factor_parameter_index);

    return std::make_unique<linear_transformation_function>(offset, factor);
}

} // namespace cosim
