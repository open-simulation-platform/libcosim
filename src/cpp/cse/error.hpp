/**
 *  \file
 *  \brief Error handling facilities.
 */
#ifndef CSE_ERROR_HPP
#define CSE_ERROR_HPP

#include <stdexcept>
#include <string>


/**
 *  \def    CSE_INPUT_CHECK(test)
 *  \brief  Checks the value of one or more function input parameters, and
 *          throws an `std::invalid_argument` if they do not fulfill the
 *          given requirements.
 *
 *  Example:
 *
 *      void foo(int x)
 *      {
 *          CSE_INPUT_CHECK(x > 0);
 *          ...
 *      }
 *
 *  If the above fails, i.e. if `x <= 0`, an exception will be thrown with
 *  the following error message:
 *
 *      Foo: Input requirement not satisfied: x > 0
 *
 *  This obviates the need to type redundant and tedious stuff like
 *
 *      if (x <= 0) throw std::invalid_argument("x must be greater than zero");
 *
 *  To ensure consistent, clear and understandable exceptions, the following
 *  guidelines should be observed when using this macro:
 *
 *    - The test expression should only include input parameters of the
 *      function/method in question, as well as literals and user-accessible
 *      symbols.  (For example, a requirement that `x > m_foo` is rather
 *      difficult for the user to comply with if `m_foo` is a private member
 *      variable.)
 *    - Since `std::invalid_argument` is a subclass of `std::logic_error`,
 *      this macro should only be used to catch logic errors, i.e. errors that
 *      are avoidable by design.  (For example, `!fileName.empty()` is probably
 *      OK, but `exists(fileName)` is not, since the  latter can only be
 *      verified at runtime.)
 *    - Use descriptive parameter names (e.g. `name` instead of `n`).
 *    - Use the same parameter names in the header and in the implementation.
 *    - Keep test expressions simple. Complicated expressions can often be
 *      written as separate tests.
 *
 *  If, for some reason, any of the above is not possible, consider writing
 *  your own specialised `if`/`throw` clause.
 *
 *  In general, it is important to keep in mind who is the target audience for
 *  the exception and its accompanying error message: namely, other developers
 *  who will be using your function, and who will be using the exception to
 *  debug their code.
 *
 *  \param[in] test An expression which can be implicitly converted to `bool`.
 */
#define CSE_INPUT_CHECK(test)                                                  \
    do {                                                                       \
        if (!(test)) {                                                         \
            throw std::invalid_argument(                                       \
                std::string(__FUNCTION__)                                      \
                + ": Input requirement not satisfied: " #test);                \
        }                                                                      \
    } while(false)

/**
 *  \def    CSE_PANIC()
 *  \brief  Prints an error message to the standard error stream and terminates
 *          the program.
 *
 *  The printed message will contain the file name and line number at which
 *  the macro is invoked.  The program is terminated by calling
 *  `std::terminate()`.
 */
#define CSE_PANIC() \
    do { ::cse::detail::panic(__FILE__, __LINE__, nullptr); } while(false)

/**
 *  \def    CSE_PANIC_M(message)
 *  \brief  Prints a custom error message to the standard error stream and
 *          terminates the program.
 *
 *  The printed message will contain the file name and line number at which
 *  the macro is invoked, in addition to the text provided in `message`.
 *  The program is terminated by calling `std::terminate()`.
 */
#define CSE_PANIC_M(message) \
    do { ::cse::detail::panic(__FILE__, __LINE__, message); } while(false)
        

namespace cse
{
namespace detail
{
    [[noreturn]] void panic(const char* file, int line, const char* msg) noexcept;
}
} // namespace
#endif // header guard
