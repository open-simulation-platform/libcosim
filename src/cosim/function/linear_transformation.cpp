/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/function/linear_transformation.hpp"

#include "cosim/function/utility.hpp"

#include <stdexcept>


using namespace std::literals;


namespace cosim
{
namespace
{

function_type_description linear_transformation_description()
{
    function_type_description f;
    f.parameters = {
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
    };
    f.io_groups = {
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
    };
    return f;
}

} // namespace


// =============================================================================
// linear_transformation_function
// =============================================================================

[[noreturn]] void throw_bad_io_ref()
{
    throw std::out_of_range("Invalid function variable reference");
}


linear_transformation_function::linear_transformation_function(double offset, double factor)
    : offset_(offset)
    , factor_(factor)
{
}

function_description linear_transformation_function::description() const
{
    return linear_transformation_description();
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
    const function_io_reference&, int)
{
    throw_bad_io_ref();
}

void linear_transformation_function::set_boolean(
    const function_io_reference&, bool)
{
    throw_bad_io_ref();
}

void linear_transformation_function::set_string(
    const function_io_reference&, std::string_view)
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
    const function_io_reference&) const
{
    throw_bad_io_ref();
}

bool linear_transformation_function::get_boolean(
    const function_io_reference&) const
{
    throw_bad_io_ref();
}

std::string_view linear_transformation_function::get_string(
    const function_io_reference&) const
{
    throw_bad_io_ref();
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
