/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/model_description.hpp"
#include "cosim/scalar_value_dispatch.hpp"
#include <cosim/exception.hpp>
#include <cosim/slave.hpp>

#include <gsl/span>
#include <gsl/span_ext>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cosim
{
namespace
{

[[noreturn]] void throw_unsupported_value(variable_type type)
{
    throw error(make_error_code(errc::unsupported_feature),
        std::string("Generic variable access is not supported for ") +
            to_text(type));
}

void validate_scalar_value(typed_value_reference variable,
    const variable_value& value)
{
    if (value.type() != variable.type) {
        throw std::invalid_argument(
            "Variable value type does not match its typed value reference");
    }
    if (!value.is_scalar()) {
        throw error(make_error_code(errc::unsupported_feature),
            "Legacy scalar slave does not support array variable access");
    }
}

void validate_supported_scalar_type(variable_type type)
{
    switch (type) {
        case variable_type::real:
        case variable_type::integer:
        case variable_type::boolean:
        case variable_type::string:
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
    throw_unsupported_value(type);
}

} // namespace

std::vector<variable_value> slave::get_variable_values(
    gsl::span<const typed_value_reference> variables) const
{
    std::vector<variable_value> result;
    result.reserve(variables.size());
    for (const auto variable : variables) {
        switch (variable.type) {
            case variable_type::real: {
                double value = 0.0;
                get_real_variables(gsl::make_span(&variable.reference, 1),
                    gsl::make_span(&value, 1));
                result.emplace_back(real_values{{value}});
                continue;
            }
            case variable_type::integer: {
                int value = 0;
                get_integer_variables(gsl::make_span(&variable.reference, 1),
                    gsl::make_span(&value, 1));
                result.emplace_back(integer_values{{value}});
                continue;
            }
            case variable_type::boolean: {
                bool value = false;
                get_boolean_variables(gsl::make_span(&variable.reference, 1),
                    gsl::make_span(&value, 1));
                result.emplace_back(boolean_values{{value}});
                continue;
            }
            case variable_type::string: {
                std::string value;
                get_string_variables(gsl::make_span(&variable.reference, 1),
                    gsl::make_span(&value, 1));
                result.emplace_back(string_values{{std::move(value)}});
                continue;
            }
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
        throw_unsupported_value(variable.type);
    }
    return result;
}

void slave::set_variable_values(
    gsl::span<const typed_value_reference> variables,
    gsl::span<const variable_value> values)
{
    if (variables.size() != values.size()) {
        throw std::invalid_argument(
            "Variable reference and value counts do not match");
    }
    if (variables.empty()) {
        return;
    }

    const auto model = model_description();
    auto valueIt = values.begin();
    for (const auto variable : variables) {
        const auto& value = *valueIt++;
        validate_scalar_value(variable, value);
        validate_supported_scalar_type(variable.type);
        bool found = false;
        for (const auto& description : model.variables) {
            if (description.type != variable.type ||
                description.reference != variable.reference) {
                continue;
            }
            found = true;
            if (!description.dimensions.empty()) {
                throw error(
                    make_error_code(errc::unsupported_feature),
                    "Legacy scalar slave does not support array variable access");
            }
        }
        if (!found) {
            throw error(make_error_code(errc::model_error),
                "Value reference " + std::to_string(variable.reference) +
                    " is not a " + to_text(variable.type) + " variable");
        }
    }

    valueIt = values.begin();
    for (const auto variable : variables) {
        detail::set_legacy_scalar_value(*this, variable, *valueIt++);
    }
}

void detail::set_legacy_scalar_value(slave& target,
    typed_value_reference variable,
    const variable_value& value)
{
    switch (variable.type) {
        case variable_type::real: {
            const auto scalar = std::get<real_values>(value.values()).values.at(0);
            target.set_real_variables(gsl::make_span(&variable.reference, 1),
                gsl::make_span(&scalar, 1));
            return;
        }
        case variable_type::integer: {
            const auto scalar = std::get<integer_values>(value.values()).values.at(0);
            target.set_integer_variables(gsl::make_span(&variable.reference, 1),
                gsl::make_span(&scalar, 1));
            return;
        }
        case variable_type::boolean: {
            const bool scalar = std::get<boolean_values>(value.values()).values.at(0);
            target.set_boolean_variables(gsl::make_span(&variable.reference, 1),
                gsl::make_span(&scalar, 1));
            return;
        }
        case variable_type::string: {
            const auto& scalar = std::get<string_values>(value.values()).values.at(0);
            target.set_string_variables(gsl::make_span(&variable.reference, 1),
                gsl::make_span(&scalar, 1));
            return;
        }
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
    throw_unsupported_value(variable.type);
}

} // namespace cosim
