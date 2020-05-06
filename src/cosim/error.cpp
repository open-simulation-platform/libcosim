#include "cosim/error.hpp"

#include <cstdio>
#include <exception>


namespace cosim
{
namespace detail
{


void precondition_violated(
    const char* function,
    const char* condition) noexcept
{
    std::fprintf(
        stderr,
        "%s: Precondition violated: %s\n",
        function,
        condition);
    std::fflush(stderr);
    std::terminate();
}


void panic(const char* file, int line, const char* msg) noexcept
{
    std::fprintf(stderr, "%s:%d: Internal error", file, line);
    if (msg != nullptr) {
        std::fprintf(stderr, ": %s", msg);
    }
    std::fputc('\n', stderr);
    std::fflush(stderr);
    std::terminate();
}
} // namespace detail
} // namespace cse
