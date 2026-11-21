/**
 *  \file
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_UTILITY_UTILITY_HPP
#define COSIM_UTILITY_UTILITY_HPP

#include <variant>

namespace cosim
{


/**
 *  A generic visitor that can be constructed on the fly from a set of lambdas.
 *
 *  Inspired by:
 *  https://arne-mertz.de/2018/05/overload-build-a-variant-visitor-on-the-fly/
 */
template<typename... Functors>
struct visitor : Functors... // NOLINT(misc-multiple-inheritance): Overload the supplied visitor call operators.
{
    visitor(const Functors&... functors) // NOLINT(cppcoreguidelines-explicit-constructor,misc-explicit-constructor): Preserve implicit visitor construction.
        : Functors(functors)...
    {
    }

    using Functors::operator()...;
};

/**
 * Utility for printing value of variant types like `scalar_value`
 *
 * See https://stackoverflow.com/questions/47168477/how-to-stream-stdvariant
 */
template<class T>
struct streamer
{
    const T& val; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members): Stream the referenced value without copying it.
};

template<class T>
streamer(T) -> streamer<T>;

template<class T>
std::ostream& operator<<(std::ostream& output, streamer<T> value)
{
    output << value.val;
    return output;
}

template<class... Ts>
std::ostream& operator<<(std::ostream& output, streamer<std::variant<Ts...>> value)
{
    std::visit([&output](const auto& element) { output << streamer{element}; }, value.val);
    return output;
}

} // namespace cosim
#endif // header guard
