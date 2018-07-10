#include "cse/error.hpp"

#include <cstdio>
#include <exception>


namespace cse
{
namespace detail
{
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
}
} // namespace
