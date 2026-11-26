/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include <cosim/algorithm/simulator.hpp>
#include <cosim/function/function.hpp>

#include "cosim/error.hpp"
#include <cosim/exception.hpp>

namespace cosim
{
namespace
{
[[noreturn]] void throw_unsupported(variable_type type)
{
    throw error(make_error_code(errc::unsupported_feature),
        std::string("Whole-value access is not supported for ") + to_text(type));
}

const function_io_description& function_io(
    const function_description& description, const function_io_reference& reference)
{
    if (reference.group < 0 ||
        static_cast<std::size_t>(reference.group) >= description.io_groups.size()) {
        throw std::out_of_range("Invalid function variable group reference");
    }
    const auto& group = description.io_groups[reference.group];
    const auto groupCount = std::get_if<int>(&group.count);
    if (!groupCount) {
        throw std::logic_error("Function instance description contains a parameter placeholder");
    }
    if (reference.group_instance < 0 || reference.group_instance >= *groupCount) {
        throw std::out_of_range("Invalid function variable group instance");
    }
    if (reference.io < 0 || static_cast<std::size_t>(reference.io) >= group.ios.size()) {
        throw std::out_of_range("Invalid function variable reference");
    }
    const auto& io = group.ios[reference.io];
    const auto ioCount = std::get_if<int>(&io.count);
    if (!ioCount) {
        throw std::logic_error("Function instance description contains a parameter placeholder");
    }
    if (reference.io_instance < 0 || reference.io_instance >= *ioCount) {
        throw std::out_of_range("Invalid function variable instance");
    }
    return io;
}

variable_type function_io_type(const function_io_description& io)
{
    const auto type = std::get_if<variable_type>(&io.type);
    if (!type) {
        throw std::logic_error("Function instance description contains a parameter placeholder");
    }
    return *type;
}
} // namespace

void function::set_variable_value(
    const function_io_reference& reference, const variable_value& value)
{
    const auto io = function_io(description(), reference);
    if (io.causality != variable_causality::input) {
        throw std::invalid_argument("Cannot set a function output variable");
    }
    const auto type = function_io_type(io);
    if (value.type() != type || !value.is_scalar()) {
        throw std::invalid_argument("Function variable value type or shape mismatch");
    }
    switch (type) {
        case variable_type::real:
            set_real(reference, std::get<real_values>(value.values()).values[0]);
            return;
        case variable_type::integer:
            set_integer(reference, std::get<integer_values>(value.values()).values[0]);
            return;
        case variable_type::boolean:
            set_boolean(reference, std::get<boolean_values>(value.values()).values[0]);
            return;
        case variable_type::string:
            set_string(reference, std::get<string_values>(value.values()).values[0]);
            return;
        case variable_type::enumeration:
        case variable_type::float32:
        case variable_type::int8:
        case variable_type::uint8:
        case variable_type::int16:
        case variable_type::uint16:
        case variable_type::uint32:
        case variable_type::int64:
        case variable_type::uint64:
        case variable_type::binary:
        case variable_type::clock:
            break;
    }
    throw_unsupported(type);
}

variable_value function::get_variable_value(const function_io_reference& reference) const
{
    const auto io = function_io(description(), reference);
    const auto type = function_io_type(io);
    switch (type) {
        case variable_type::real:
            return variable_value(real_values{{get_real(reference)}});
        case variable_type::integer:
            return variable_value(integer_values{{get_integer(reference)}});
        case variable_type::boolean:
            return variable_value(boolean_values{{get_boolean(reference)}});
        case variable_type::string:
            return variable_value(string_values{{std::string(get_string(reference))}});
        case variable_type::enumeration:
        case variable_type::float32:
        case variable_type::int8:
        case variable_type::uint8:
        case variable_type::int16:
        case variable_type::uint16:
        case variable_type::uint32:
        case variable_type::int64:
        case variable_type::uint64:
        case variable_type::binary:
        case variable_type::clock:
            break;
    }
    throw_unsupported(type);
}

void manipulable::set_variable_input_modifier(
    variable_type, value_reference, variable_modifier)
{
    throw error(make_error_code(errc::unsupported_feature),
        "Whole-value input modifiers are not supported by this simulator");
}

void manipulable::set_variable_output_modifier(
    variable_type, value_reference, variable_modifier)
{
    throw error(make_error_code(errc::unsupported_feature),
        "Whole-value output modifiers are not supported by this simulator");
}

variable_value observable::get_variable_value(
    variable_type type, value_reference reference) const
{
    switch (type) {
        case variable_type::real:
            return variable_value(real_values{{get_real(reference)}});
        case variable_type::integer:
            return variable_value(integer_values{{get_integer(reference)}});
        case variable_type::boolean:
            return variable_value(boolean_values{{get_boolean(reference)}});
        case variable_type::string:
            return variable_value(string_values{{std::string(get_string(reference))}});
        case variable_type::enumeration:
        case variable_type::float32:
        case variable_type::int8:
        case variable_type::uint8:
        case variable_type::int16:
        case variable_type::uint16:
        case variable_type::uint32:
        case variable_type::int64:
        case variable_type::uint64:
        case variable_type::binary:
        case variable_type::clock:
            break;
    }
    throw_unsupported(type);
}

void simulator::set_variable_value(
    variable_type type, value_reference reference, const variable_value& value)
{
    COSIM_INPUT_CHECK(value.type() == type);
    COSIM_INPUT_CHECK(value.is_scalar());
    switch (type) {
        case variable_type::real:
            set_real(reference, std::get<real_values>(value.values()).values[0]);
            return;
        case variable_type::integer:
            set_integer(reference, std::get<integer_values>(value.values()).values[0]);
            return;
        case variable_type::boolean:
            set_boolean(reference, std::get<boolean_values>(value.values()).values[0]);
            return;
        case variable_type::string:
            set_string(reference, std::get<string_values>(value.values()).values[0]);
            return;
        case variable_type::enumeration:
        case variable_type::float32:
        case variable_type::int8:
        case variable_type::uint8:
        case variable_type::int16:
        case variable_type::uint16:
        case variable_type::uint32:
        case variable_type::int64:
        case variable_type::uint64:
        case variable_type::binary:
        case variable_type::clock:
            break;
    }
    throw_unsupported(type);
}
} // namespace cosim
