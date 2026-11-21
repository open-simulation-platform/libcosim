/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_XML_CLOCK_HPP
#define COSIM_XML_CLOCK_HPP

#include "cosim/xml_value.hpp"

#include <array>

namespace cosim::detail
{

inline clock_interval_variability
parse_clock_variability(std::string_view text)
{
    using V = clock_interval_variability;
    constexpr std::array<std::pair<std::string_view, V>, 6> choices{
        {
            {"constant", V::constant},
            {"fixed", V::fixed},
            {"tunable", V::tunable},
            {"changing", V::changing},
            {"countdown", V::countdown},
            {"triggered", V::triggered},
        },
    };
    for (const auto& [name, type] : choices) {
        if (text == name) {
            return type;
        }
    }
    throw std::invalid_argument("Invalid Clock intervalVariability: " +
        std::string(text));
}

template<typename T, typename GetAttribute>
std::optional<T> xml_clock_attribute(const GetAttribute& get,
    const char* name)
{
    const auto text = get(name);
    if (!text) {
        return std::nullopt;
    }
    const auto trimmed = trim_xml_whitespace(*text);
    if constexpr (std::is_same_v<T, clock_interval_variability>) {
        return parse_clock_variability(trimmed);
    } else if constexpr (std::is_same_v<T, bool>) {
        return std::get<bool>(
            parse_xml_scalar_value(variable_type::boolean, trimmed));
    } else if constexpr (std::is_same_v<T, double>) {
        return parse_xml_float<double>(trimmed);
    } else {
        return parse_xml_integer<T>(trimmed);
    }
}

} // namespace cosim::detail
#endif
