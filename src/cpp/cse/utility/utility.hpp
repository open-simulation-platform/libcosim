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

/**
 * Utility for printing value of variant types like `scalar_value`
 *
 * See https://stackoverflow.com/questions/47168477/how-to-stream-stdvariant
 */
template<class T>
struct streamer
{
    const T& val;
};

template<class T>
streamer(T)->streamer<T>;

template<class T>
std::ostream& operator<<(std::ostream& os, streamer<T> s)
{
    os << s.val;
    return os;
}

template<class... Ts>
std::ostream& operator<<(std::ostream& os, streamer<std::variant<Ts...>> sv)
{
    std::visit([&os](const auto& v) { os << streamer{v}; }, sv.val);
    return os;
}

} // namespace cse
#endif // header guard
