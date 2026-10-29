/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_UTILITY_ENUMERATION_HPP
#define COSIM_UTILITY_ENUMERATION_HPP

#include <cosim/model_description.hpp>

#include <optional>

namespace cosim
{

inline std::optional<variable_value> normalize_enumeration_value(
    variable_type targetType,
    const variable_value& value)
{
    if (targetType != variable_type::enumeration ||
        value.type() != variable_type::integer || !value.is_scalar()) {
        return std::nullopt;
    }
    return variable_value(enumeration_values{{
        enumeration_value{std::get<integer_values>(value.values()).values[0]}}});
}

inline void normalize_enumeration_value(
    variable_type targetType,
    scalar_value& value)
{
    if (targetType == variable_type::enumeration) {
        if (const auto integer = std::get_if<int>(&value)) {
            value = enumeration_value{*integer};
        }
    }
}

} // namespace cosim
#endif
