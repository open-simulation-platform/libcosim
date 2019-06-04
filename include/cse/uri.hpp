/**
 *  \file
 *  \brief URI parsing and handling.
 */
#ifndef CSE_URI_HPP
#define CSE_URI_HPP

#include <boost/filesystem.hpp>

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
     *  Passing an empty `path` and `std::nullopt` for all other components is
     *  equivalent to calling the default constructor.
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


/**
 *  Percent-encodes a string.
 *
 *  All characters will be encoded, with the exception of those that are
 *  classified as "unreserved characters" in
 *  [RFC 3986](https://tools.ietf.org/html/rfc3986) and those in `exceptions`.
 *
 *  \param [in] string
 *      The string to encode.
 *  \param [in] exceptions
 *      A string containing additional characters that are *not* to be encoded,
 *      e.g. if they have a special meaning in a particular URI scheme.
 *
 *  \returns
 *      A new, percent-encoded string.
 */
std::string percent_encode(std::string_view string, const char* exceptions = nullptr);


/**
 *  Decodes a percent-encoded string.
 *
 *  \param [in] encoded
 *      A percent-encoded string.
 *
 *  \returns
 *      A new, decoded string.
 */
std::string percent_decode(std::string_view encoded);


/**
 *  Composes a percent-encoded URI from (unencoded) components.
 *
 *  This will percent-encode each component according to some rules that work
 *  with many URI schemes, but not necessarily all.  The characters that are
 *  left unencoded (beyond the "unreserved character set") are specified in
 *  the parameter descriptions below.
 *
 *  \param [in] scheme
 *      The URI scheme component. Un-encoded characters: `+`
 *  \param [in] authority
 *      The authority component. Un-encoded characters: `@:+`
 *  \param [in] path
 *      The path component. Un-encoded characters: `/+`
 *  \param [in] query
 *      The query component. Un-encoded characters: `=&;/:+`
 *  \param [in] fragment
 *      The fragment component. Un-encoded characters: (none)
 *
 *  \returns
 *      The composed and encoded URI.
 */
uri percent_encode_uri(
    std::optional<std::string_view> scheme,
    std::optional<std::string_view> authority,
    std::string_view path,
    std::optional<std::string_view> query = std::nullopt,
    std::optional<std::string_view> fragment = std::nullopt);


/**
 *  Converts a local filesystem path to a `file` URI.
 *
 *  \param [in] path
 *      A path that either satisfies `path.has_root_directory()` or is empty.
 *
 *  \returns
 *      The URI that corresponds to `path`.  The general format will be
 *      `file:///<os-dependent path>`, except when `path` is empty, in which
 *      case the function returns `file:`.
 */
uri path_to_file_uri(const boost::filesystem::path& path);


/**
 *  Converts a `file` URI to a local filesystem path.
 *
 *  \param [in] fileUri
 *      An URI where the scheme component is equal to `file` and the `authority`
 *      component is either empty or equel to `localhost` (but not undefined).
 *
 *  \returns
 *      The path that corresponds to `fileUri`.
 */
boost::filesystem::path file_uri_to_path(const uri& fileUri);


} // namespace cse
#endif // header guard
