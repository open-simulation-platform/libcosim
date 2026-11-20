/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef LIBCOSIM_FMI_GLUE_INTERNAL_HPP
#define LIBCOSIM_FMI_GLUE_INTERNAL_HPP

#include <optional>
#include <string>

namespace cosim::fmi::glue_detail
{

inline const char* nonnull(const char* value)
{
    return value == nullptr ? "" : value;
}

inline std::optional<std::string> optional_string(const char* value)
{
    return value == nullptr ? std::nullopt : std::optional<std::string>(value);
}

} // namespace cosim::fmi::glue_detail

#endif
