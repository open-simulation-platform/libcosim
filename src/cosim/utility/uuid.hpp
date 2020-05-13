/**
 *  \file
 *  Utility functions for dealing with UUIDs.
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_UTILITY_UUID_HPP
#define COSIM_UTILITY_UUID_HPP

#include <string>


namespace cosim
{
namespace utility
{


/// Returns a randomly generated UUID.
std::string random_uuid() noexcept;


} // namespace utility
} // namespace cosim
#endif // header guard
