#include <cse/hello_world.hpp>

#include <algorithm>
#include <cstring>


namespace cse
{


std::size_t hello_world(gsl::span<char> buffer) noexcept
{
    const auto n = (std::min)(buffer.size(), static_cast<ptrdiff_t>(12));
    std::memcpy(buffer.data(), "Hello World!", n);
    return n;
}


} // namespace
