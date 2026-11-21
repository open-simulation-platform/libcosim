/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/error.hpp"

#include <cstdio>
#include <exception>


namespace cosim::detail
{


void precondition_violated(
    const char* function,
    const char* condition) noexcept
{
    // Diagnostics are best-effort on this terminating path.
    static_cast<void>(std::fprintf( // NOLINT(cppcoreguidelines-pro-type-vararg): Preserve C stdio diagnostics on the terminating path.
        stderr,
        "%s: Precondition violated: %s\n",
        function,
        condition));
    static_cast<void>(std::fflush(stderr));
    std::terminate();
}


void panic(const char* file, int line, const char* msg) noexcept
{
    // Diagnostics are best-effort on this terminating path.
    static_cast<void>(std::fprintf(stderr, "%s:%d: Internal error", file, line)); // NOLINT(cppcoreguidelines-pro-type-vararg): Preserve C stdio diagnostics on the terminating path.
    if (msg != nullptr) {
        static_cast<void>(std::fprintf(stderr, ": %s", msg)); // NOLINT(cppcoreguidelines-pro-type-vararg): Preserve C stdio diagnostics on the terminating path.
    }
    static_cast<void>(std::fputc('\n', stderr));
    static_cast<void>(std::fflush(stderr));
    std::terminate();
}
} // namespace cosim::detail
