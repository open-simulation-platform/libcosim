/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_UTILITY_VARIABLE_ALIAS_HPP
#define COSIM_UTILITY_VARIABLE_ALIAS_HPP

#include "cosim/model_description.hpp"

namespace cosim
{
namespace detail
{

inline variable_description describe_alias(
    const variable_description& variable,
    const variable_alias& alias)
{
    auto result = variable;
    result.name = alias.name;
    result.description = alias.description.value_or("");
    if (alias.display_unit) result.display_unit = alias.display_unit;
    result.aliases.clear();
    return result;
}

} // namespace detail
} // namespace cosim
#endif
