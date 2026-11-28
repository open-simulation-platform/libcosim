/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/function/description.hpp"
#include "cosim/model_description.hpp"
#include <cosim/exception.hpp>
#include <cosim/function/function.hpp>

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>


namespace cosim
{
namespace
{

const function_io_description& get_io_description(
    const function_description& description,
    const function_io_reference& reference)
{
    if (reference.group < 0 ||
        static_cast<std::size_t>(reference.group) >= description.io_groups.size()) {
        throw std::out_of_range("Invalid function variable group reference");
    }
    const auto& group = description.io_groups.at(reference.group);
    const auto* const groupCount = std::get_if<int>(&group.count);
    if (groupCount == nullptr) {
        throw std::logic_error(
            "Function instance description contains a parameter placeholder");
    }
    if (reference.group_instance < 0 ||
        reference.group_instance >= *groupCount) {
        throw std::out_of_range("Invalid function variable group instance");
    }
    if (reference.io < 0 ||
        static_cast<std::size_t>(reference.io) >= group.ios.size()) {
        throw std::out_of_range("Invalid function variable reference");
    }
    const auto& inputOutput = group.ios.at(reference.io);
    const auto* const ioCount = std::get_if<int>(&inputOutput.count);
    if (ioCount == nullptr) {
        throw std::logic_error(
            "Function instance description contains a parameter placeholder");
    }
    if (reference.io_instance < 0 || reference.io_instance >= *ioCount) {
        throw std::out_of_range("Invalid function variable instance");
    }
    return inputOutput;
}


variable_type get_io_type(const function_io_description& description)
{
    const auto* const type = std::get_if<variable_type>(&description.type);
    if (type == nullptr) {
        throw std::logic_error(
            "Function instance description contains a parameter placeholder");
    }
    return *type;
}


std::vector<std::uint64_t> get_io_dimensions(
    const function_io_description& description)
{
    std::vector<std::uint64_t> dimensions;
    dimensions.reserve(description.dimensions.size());
    for (const auto& dimension : description.dimensions) {
        const auto* const concrete = std::get_if<std::uint64_t>(&dimension);
        if (concrete == nullptr) {
            throw std::logic_error(
                "Function instance description contains a parameter placeholder");
        }
        dimensions.push_back(*concrete);
    }
    return dimensions;
}


[[noreturn]] void throw_unsupported(variable_type type)
{
    throw error(
        make_error_code(errc::unsupported_feature),
        std::string("Whole-value function access is not supported for ") +
            to_text(type));
}

} // namespace


void function::set_variable_value(
    const function_io_reference& reference,
    const variable_value& value)
{
    const auto inputOutput = get_io_description(description(), reference);
    if (inputOutput.causality != variable_causality::input) {
        throw std::invalid_argument("Cannot set a function output variable");
    }
    const auto type = get_io_type(inputOutput);
    if (value.type() != type || value.dimensions() != get_io_dimensions(inputOutput)) {
        throw std::invalid_argument(
            "Function variable value type or shape mismatch");
    }
    if (!value.is_scalar()) {
        throw_unsupported(type);
    }

    switch (type) {
        case variable_type::real:
            set_real(
                reference,
                std::get<real_values>(value.values()).values.at(0));
            return;
        case variable_type::integer:
            set_integer(
                reference,
                std::get<integer_values>(value.values()).values.at(0));
            return;
        case variable_type::boolean:
            set_boolean(
                reference,
                std::get<boolean_values>(value.values()).values.at(0));
            return;
        case variable_type::string:
            set_string(
                reference,
                std::get<string_values>(value.values()).values.at(0));
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


variable_value function::get_variable_value(
    const function_io_reference& reference) const
{
    const auto inputOutput = get_io_description(description(), reference);
    const auto type = get_io_type(inputOutput);
    if (!get_io_dimensions(inputOutput).empty()) {
        throw_unsupported(type);
    }

    switch (type) {
        case variable_type::real:
            return variable_value(real_values{{get_real(reference)}});
        case variable_type::integer:
            return variable_value(integer_values{{get_integer(reference)}});
        case variable_type::boolean:
            return variable_value(boolean_values{{get_boolean(reference)}});
        case variable_type::string:
            return variable_value(
                string_values{{std::string(get_string(reference))}});
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
