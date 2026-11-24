/**
 *  \file
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef LIBCOSIM_FS_PORTABILITY_HPP
#define LIBCOSIM_FS_PORTABILITY_HPP

#if __has_include(<filesystem>)
#    include <filesystem> // IWYU pragma: export
namespace cosim
{
namespace filesystem = std::filesystem; // NOLINT(misc-unused-alias-decls): Public portability alias.
}
#else
#    include <experimental/filesystem> // IWYU pragma: export
namespace cosim
{
namespace filesystem = std::experimental::filesystem; // NOLINT(misc-unused-alias-decls): Public portability alias.
}
#endif

#endif // LIBCOSIM_FS_PORTABILITY_HPP
