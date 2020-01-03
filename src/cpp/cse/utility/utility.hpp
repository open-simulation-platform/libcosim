#ifndef CSE_UTILITY_UTILITY_HPP
#define CSE_UTILITY_UTILITY_HPP


namespace cse
{


/**
 *  A generic visitor that can be constructed on the fly from a set of lambdas.
 *
 *  Inspired by:
 *  https://arne-mertz.de/2018/05/overload-build-a-variant-visitor-on-the-fly/
 */
template<typename... Functors>
struct visitor : Functors...
{
    visitor(const Functors&... functors)
        : Functors(functors)...
    {
    }

    using Functors::operator()...;
};


} // namespace cse
#endif // header guard
