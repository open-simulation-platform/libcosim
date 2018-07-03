/**
 *  \file
 *  \brief  Hello World!
 */
#ifndef CSE_HELLO_WORLD_HPP
#define CSE_HELLO_WORLD_HPP

#include <cstddef>
#include <gsl/span>


namespace cse
{


/**
 *  \brief  Writes the string "Hello World!" to a character buffer.
 *
 *  If the buffer is not large enough, the string will be truncated.
 *  No terminating zero byte will be written.
 *
 *  \param buffer
 *      A buffer to hold the friendly greeting.
 *
 *  \returns
 *      The number of bytes written.
 */
std::size_t hello_world(gsl::span<char> buffer) noexcept;


/**
 *  \brief  Returns the answer to the ultimate question about life,
 *          the universe and everything.
 */
constexpr int get_ultimate_answer() noexcept
{
    return 42;
}


} // namespace
#endif // header guard
