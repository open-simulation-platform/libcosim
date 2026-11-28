/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef LIBCOSIM_EVENT_VALUE_HPP
#define LIBCOSIM_EVENT_VALUE_HPP

#include "cosim/event_iteration.hpp"
#include "cosim/model_description.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace cosim::detail
{

/// Compares event samples, treating two floating-point NaNs as unchanged.
template<typename T>
bool event_values_equal(const T& lhs, const T& rhs)
{
    if constexpr (std::is_floating_point_v<T>) {
        return lhs == rhs || (std::isnan(lhs) && std::isnan(rhs));
    } else {
        return lhs == rhs;
    }
}

/// Compares type, shape and values, including NaNs in floating-point arrays.
inline bool event_values_equal(const variable_value& lhs, const variable_value& rhs)
{
    if (lhs.type() != rhs.type() || lhs.dimensions() != rhs.dimensions()) {
        return false;
    }
    if (lhs.type() == variable_type::real) {
        const auto& left = std::get<real_values>(lhs.values()).values;
        const auto& right = std::get<real_values>(rhs.values()).values;
        return left.size() == right.size() &&
            std::equal(left.begin(), left.end(), right.begin(),
                [](double x, double y) { return event_values_equal(x, y); });
    }
    if (lhs.type() == variable_type::float32) {
        const auto& left = std::get<float32_values>(lhs.values()).values;
        const auto& right = std::get<float32_values>(rhs.values()).values;
        return left.size() == right.size() &&
            std::equal(left.begin(), left.end(), right.begin(),
                [](float x, float y) { return event_values_equal(x, y); });
    }
    return lhs == rhs;
}

inline bool event_values_close(double lhs, double rhs, event_value_tolerance tolerance)
{
    if (event_values_equal(lhs, rhs)) {
        return true;
    }
    if (!std::isfinite(lhs) || !std::isfinite(rhs) ||
        (tolerance.absolute == 0.0 && tolerance.relative == 0.0)) {
        return false;
    }
    const auto scale = std::max(std::abs(lhs), std::abs(rhs));
    const auto difference = std::abs(lhs - rhs);
    const auto limit = tolerance.absolute + (tolerance.relative * scale);
    if (std::isfinite(difference)) {
        return difference <= limit;
    }
    if (std::isfinite(limit)) {
        return false;
    }
    return std::abs((lhs / scale) - (rhs / scale)) <=
        (tolerance.absolute / scale) + tolerance.relative;
}

inline bool event_values_close(
    const variable_value& lhs, const variable_value& rhs, event_value_tolerance tolerance)
{
    if (lhs.type() != rhs.type() || lhs.dimensions() != rhs.dimensions()) {
        return false;
    }
    const auto close = [tolerance](const auto& left, const auto& right) {
        return std::equal(left.values.begin(), left.values.end(),
            right.values.begin(), right.values.end(),
            [tolerance](auto x, auto y) { return event_values_close(x, y, tolerance); });
    };
    if (lhs.type() == variable_type::real) {
        return close(std::get<real_values>(lhs.values()), std::get<real_values>(rhs.values()));
    }
    if (lhs.type() == variable_type::float32) {
        return close(std::get<float32_values>(lhs.values()), std::get<float32_values>(rhs.values()));
    }
    return lhs == rhs;
}

} // namespace cosim::detail

#endif
