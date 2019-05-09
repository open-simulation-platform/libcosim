/**
 *  \file
 *  \brief URI parsing and handling.
 */
#ifndef CSE_URI_HPP
#define CSE_URI_HPP

#include <optional>
#include <ostream>
#include <string>
#include <string_view>


namespace cse
{

/**
 *  A URI reference.
 *
 *  A URI reference is an (absolute) URI if and only if it has a *scheme*
 *  component, i.e., the segment leading up to the first colon character.
 *  (For example: the `http` part of `http://example.com`).
 */
class uri
{
public:
    /// Constructs an empty URI reference.
    uri() noexcept;

    /**
     *  Parses the contents of `string`.
     *
     *  `string` must either contain a valid URI reference or be empty.
     *  Passing an empty string is equivalent to calling the default
     *  constructor.
     *
     *  Complies with [RFC 3986](https://tools.ietf.org/html/rfc3986). 
     *  The "authority" component is not validated or decomposed.
     *
     *  \throws std::invalid_argument
     *      if `string` does not contain a valid URI reference.
     */
    /*implicit*/ uri(std::string string);

    // Enable indirect implicit conversions from various string types.
    /*implicit*/ uri(std::string_view string)
        : uri(std::string(string))
    {}

    /*implicit*/ uri(const char* string)
        : uri(std::string(string))
    {}

    /**
     *  Composes a URI reference from its individual components.
     *
     *  Each component must conform to the rules described in RFC 3986.
     *  Beyond that, no validation is performed. (That is, no hostname lookup,
     *  no scheme-specific validation, and so on).
     *
     *  Passing an empty `path` and null for all other components is equivalent
     *  to calling the default constructor.
     *
     *  \throws std::invalid_argument
     *      if any of the components have an invalid value.
     */
    uri(
        std::optional<std::string_view> scheme,
        std::optional<std::string_view> authority,
        std::string_view path,
        std::optional<std::string_view> query = std::nullopt,
        std::optional<std::string_view> fragment = std::nullopt);

    /**
     *  Returns the entire URI reference as a string.
     *
     *  The returned `std::string_view` is only valid as long as the `uri`
     *  object remains alive and unmodified.
     */
    std::string_view view() const noexcept
    {
        return std::string_view(data_);
    }

    /**
     *  Returns the scheme component, or null if there is none.
     *
     *  The returned `std::string_view` is only valid as long as the `uri`
     *  object remains alive and unmodified.
     */
    std::optional<std::string_view> scheme() const noexcept
    {
        return scheme_;
    }

    /**
     *  Returns the authority component, or null if there is none.
     *
     *  The returned `std::string_view` is only valid as long as the `uri`
     *  object remains alive and unmodified.
     */
    std::optional<std::string_view> authority() const noexcept
    {
        return authority_;
    }

    /**
     *  Returns the path component.
     *
     *  The returned `std::string_view` is only valid as long as the `uri`
     *  object remains alive and unmodified.
     */
    std::string_view path() const noexcept
    {
        return path_;
    }

    /**
     *  Returns the query component, or null if there is none.
     *
     *  The returned `std::string_view` is only valid as long as the `uri`
     *  object remains alive and unmodified.
     */
    std::optional<std::string_view> query() const noexcept
    {
        return query_;
    }

    /**
     *  Returns the fragment component, or null if there is none.
     *
     *  The returned `std::string_view` is only valid as long as the `uri`
     *  object remains alive and unmodified.
     */
    std::optional<std::string_view> fragment() const noexcept
    {
        return fragment_;
    }

    /// Returns whether the `uri` object is empty.
    bool empty() const noexcept
    {
        return data_.empty();
    }

private:
    std::string data_;

    std::optional<std::string_view> scheme_;
    std::optional<std::string_view> authority_;
    std::string_view path_;
    std::optional<std::string_view> query_;
    std::optional<std::string_view> fragment_;
};


/// Compares two URI references for equality.
inline bool operator==(const uri& a, const uri& b) noexcept
{
    return a.view() == b.view();
}

/// Compares two URI references for inequality.
inline bool operator!=(const uri& a, const uri& b) noexcept
{
    return a.view() != b.view();
}


/// Writes a URI reference to an output stream.
inline std::ostream& operator<<(std::ostream& stream, const uri& u)
{
    return stream << u.view();
}


/**
 *  Resolves a URI reference relative to a base URI.
 *
 *  Strictly complies with [RFC 3986](https://tools.ietf.org/html/rfc3986). 
 *
 *  \param [in] base
 *      An absolute URI.
 *  \param [in] reference
 *      A URI reference, which may be relative.
 *
 *  \throws std::invalid_argument
 *      if `base` is not absolute.
 */
uri resolve_reference(const uri& base, const uri& reference);


} // namespace cse
#endif // header guard
