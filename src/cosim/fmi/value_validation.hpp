/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_FMI_VALUE_VALIDATION_HPP
#define COSIM_FMI_VALUE_VALIDATION_HPP

#include "cosim/error.hpp"
#include "cosim/fmi/enumeration.hpp"
#include "cosim/utility/enumeration.hpp"

#include <gsl/span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cosim
{
namespace fmi
{
namespace detail
{

template<typename ValidateEnumeration>
std::vector<variable_value> prevalidate_legacy_values(
    const model_description& model,
    const std::string& instanceName,
    gsl::span<const typed_value_reference> variables,
    gsl::span<const variable_value> values,
    ValidateEnumeration validateEnumeration)
{
    COSIM_INPUT_CHECK(variables.size() == values.size());
    std::vector<variable_value> normalized;
    normalized.reserve(values.size());
    for (std::size_t i = 0; i < variables.size(); ++i) {
        const auto variable = variables[i];
        auto value = normalize_enumeration_value(variable.type, values[i]);
        normalized.push_back(value ? std::move(*value) : values[i]);
        const auto& item = normalized.back();
        if (item.type() != variable.type) {
            throw std::invalid_argument("Variable value type does not match its typed value reference");
        }
        if (!item.is_scalar()) {
            throw error(make_error_code(errc::unsupported_feature),
                "Legacy scalar slave does not support array variable access");
        }
        switch (variable.type) {
            case variable_type::real:
            case variable_type::integer:
            case variable_type::boolean:
            case variable_type::string:
            case variable_type::enumeration:
                break;
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
                throw error(make_error_code(errc::unsupported_feature),
                    std::string("Generic variable access is not supported for ") + to_text(variable.type));
        }
        if (find_variable_description(model, variable.type, variable.reference) == nullptr) {
            throw error(make_error_code(errc::model_error),
                instanceName + ": value reference " + std::to_string(variable.reference) +
                    " is not a " + to_text(variable.type) + " variable");
        }
        if (variable.type == variable_type::enumeration) {
            validateEnumeration(variable.reference,
                std::get<enumeration_values>(item.values()).values[0]);
        }
    }
    return normalized;
}

} // namespace detail
} // namespace fmi
} // namespace cosim
#endif
