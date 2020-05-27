/**
 *  \file
 *  Information about this library.
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_LIB_INFO_HPP
#define COSIM_LIB_INFO_HPP


namespace cosim
{


/**
 *  Short form of the library name, guaranteed to contain only alphanumeric
 *  characters, dashes and underscores (no spaces).
 */
constexpr const char* library_short_name = "libcosim";


/// Software version
struct version
{
    int major = 0;
    int minor = 0;
    int patch = 0;
};


/// Returns the version of the libcosim library.
version library_version();


} // namespace cosim
#endif // header guard
