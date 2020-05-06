#include "cse/uri.hpp"

#include "error.hpp"

#include <cassert>
#include <cctype>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#    include <shlwapi.h>
#    include <wininet.h>
#endif


namespace cosim
{

// =============================================================================
// class uri
// =============================================================================

namespace
{

bool is_scheme_char(char c) noexcept
{
    return std::isalnum(static_cast<unsigned char>(c)) ||
        c == '+' || c == '-' || c == '.';
}

std::optional<std::string_view> consume_scheme(std::string_view& input)
{
    std::size_t pos = 0;
    while (pos < input.size() && is_scheme_char(input[pos])) ++pos;
    if (pos == input.size() || input[pos] != ':') {
        return std::nullopt;
    } else if (pos == 0) {
        throw std::invalid_argument("URI scheme is empty");
    } else {
        auto ret = input.substr(0, pos);
        input.remove_prefix(pos + 1);
        return ret;
    }
}

std::string_view consume_while(
    bool (*is_valid_char)(char),
    std::string_view& input,
    std::size_t start) noexcept
{
    std::size_t pos = start;
    while (pos < input.size() && is_valid_char(input[pos])) ++pos;
    auto ret = input.substr(start, pos - start);
    input.remove_prefix(pos);
    return ret;
}

bool is_authority_char(char c) noexcept
{
    return c != '/' && c != '?' && c != '#';
}

std::optional<std::string_view> consume_authority(std::string_view& input)
{
    if (input.size() < 2 || input[0] != '/' || input[1] != '/') {
        return std::nullopt;
    }
    return consume_while(is_authority_char, input, 2);
}

bool is_path_char(char c) noexcept
{
    return c != '?' && c != '#';
}

std::string_view consume_path(std::string_view& input)
{
    return consume_while(is_path_char, input, 0);
}

bool is_query_char(char c) noexcept
{
    return c != '#';
}

std::optional<std::string_view> consume_query(std::string_view& input)
{
    if (input.empty() || input[0] != '?') {
        return std::nullopt;
    }
    return consume_while(is_query_char, input, 1);
}

std::optional<std::string_view> consume_fragment(std::string_view& input)
{
    if (input.empty()) {
        return std::nullopt;
    }
    assert(input[0] == '#');
    auto ret = input.substr(1);
    input.remove_prefix(input.size());
    return ret;
}

bool all_chars_satisfy(bool (*is_valid_char)(char), std::string_view string)
{
    for (auto c : string) {
        if (!is_valid_char(c)) return false;
    }
    return true;
}

detail::subrange to_subrange(const std::string& str, std::string_view substr)
{
    assert( // Check that substr is in fact a substring of str
        reinterpret_cast<std::uintptr_t>(str.data()) <=
            reinterpret_cast<std::uintptr_t>(substr.data()) &&
        reinterpret_cast<std::uintptr_t>(substr.data()) + substr.size() <=
            reinterpret_cast<std::uintptr_t>(str.data()) + str.size());
    return {static_cast<std::size_t>(substr.data() - str.data()), substr.size()};
}

std::optional<detail::subrange> to_subrange(
    const std::string& str,
    std::optional<std::string_view> substr)
{
    if (substr) {
        return to_subrange(str, *substr);
    } else {
        return std::nullopt;
    }
}

std::string_view to_substring(const std::string& str, detail::subrange subrange)
{
    return std::string_view(str).substr(subrange.offset, subrange.size);
}

std::optional<std::string_view> to_substring(
    const std::string& str,
    std::optional<detail::subrange> subrange)
{
    if (subrange) {
        return to_substring(str, *subrange);
    } else {
        return std::nullopt;
    }
}

} // namespace


uri::uri() noexcept = default;


/*implicit*/ uri::uri(std::string string)
    : data_(std::move(string))
{
    auto view = std::string_view(data_);
    scheme_ = to_subrange(data_, consume_scheme(view));
    authority_ = to_subrange(data_, consume_authority(view));
    path_ = to_subrange(data_, consume_path(view));
    query_ = to_subrange(data_, consume_query(view));
    fragment_ = to_subrange(data_, consume_fragment(view));
    assert(view.empty());
}


uri::uri(
    std::optional<std::string_view> scheme,
    std::optional<std::string_view> authority,
    std::string_view path,
    std::optional<std::string_view> query,
    std::optional<std::string_view> fragment)
{
    CSE_INPUT_CHECK(!scheme ||
        (!scheme->empty() && all_chars_satisfy(is_scheme_char, *scheme)));
    CSE_INPUT_CHECK(!authority || all_chars_satisfy(is_authority_char, *authority));
    CSE_INPUT_CHECK(!authority || path.empty() || path.front() == '/');
    CSE_INPUT_CHECK(all_chars_satisfy(is_path_char, path));
    CSE_INPUT_CHECK(!query || all_chars_satisfy(is_query_char, path));
    data_.reserve(
        (scheme ? scheme->size() + 1 : 0) +
        (authority ? authority->size() + 2 : 0) +
        path.size() +
        (query ? query->size() + 1 : 0) +
        (fragment ? fragment->size() + 1 : 0));
    auto view = std::string_view(data_.data(), data_.capacity());
    if (scheme) {
        data_ += *scheme;
        data_ += ':';
        scheme_ = to_subrange(data_, view.substr(0, scheme->size()));
        view.remove_prefix(scheme->size() + 1);
    }
    if (authority) {
        data_ += "//";
        data_ += *authority;
        authority_ = to_subrange(data_, view.substr(2, authority->size()));
        view.remove_prefix(2 + authority->size());
    }
    data_ += path;
    path_ = to_subrange(data_, view.substr(0, path.size()));
    view.remove_prefix(path.size());
    if (query) {
        data_ += '?';
        data_ += *query;
        query_ = to_subrange(data_, view.substr(1, query->size()));
        view.remove_prefix(1 + query->size());
    }
    if (fragment) {
        data_ += '#';
        data_ += *fragment;
        fragment_ = to_subrange(data_, view.substr(1, fragment->size()));
        view.remove_prefix(1 + fragment->size());
    }
}


std::string_view uri::view() const noexcept
{
    return std::string_view(data_);
}


std::optional<std::string_view> uri::scheme() const noexcept
{
    return to_substring(data_, scheme_);
}


std::optional<std::string_view> uri::authority() const noexcept
{
    return to_substring(data_, authority_);
}


std::string_view uri::path() const noexcept
{
    return to_substring(data_, path_);
}


std::optional<std::string_view> uri::query() const noexcept
{
    return to_substring(data_, query_);
}


std::optional<std::string_view> uri::fragment() const noexcept
{
    return to_substring(data_, fragment_);
}


bool uri::empty() const noexcept
{
    return data_.empty();
}


// =============================================================================
// resolve_reference()
// =============================================================================

namespace
{

// Canonical `merge` algorithm from RFC 3986 Sec. 5.2.3.
std::string merge(
    bool baseHasAuthority,
    std::string_view basePath,
    std::string_view referencePath)
{
    std::string result;
    if (baseHasAuthority && basePath.empty()) {
        result = "/";
    } else {
        const auto lastSlash = basePath.find_last_of('/');
        const auto split = (lastSlash == std::string_view::npos) ? 0 : lastSlash + 1;
        result = basePath.substr(0, split);
    }
    result += referencePath;
    return result;
}

// Substitute for C++20 function `std::string_view::starts_with()`.
bool starts_with(std::string_view string, std::string_view prefix)
{
    return string.substr(0, prefix.size()) == prefix;
}

// Canonical `remove_dot_segments` algorithm from RFC 3986 Sec. 5.2.4.
//
// There exist far more efficient implementations, but this one follows
// the RFC closely.
std::string_view remove_dot_segments(
    std::string_view path,
    std::string& outputBuffer)
{
    auto inputBuffer = std::string(path);
    outputBuffer.clear();

    const auto removeLastOutputSegment = [&] {
        if (outputBuffer.empty()) return;
        outputBuffer.pop_back();
        while (!outputBuffer.empty() && outputBuffer.back() != '/') outputBuffer.pop_back();
        if (!outputBuffer.empty()) outputBuffer.pop_back();
    };

    while (!inputBuffer.empty()) {
        if (starts_with(inputBuffer, "../")) {
            inputBuffer.erase(0, 3);
        } else if (starts_with(inputBuffer, "./")) {
            inputBuffer.erase(0, 2);
        } else if (starts_with(inputBuffer, "/./")) {
            inputBuffer.erase(1, 2);
        } else if (inputBuffer == "/.") {
            inputBuffer.erase(1, 1);
        } else if (starts_with(inputBuffer, "/../")) {
            inputBuffer.erase(1, 3);
            removeLastOutputSegment();
        } else if (inputBuffer == "/..") {
            inputBuffer.erase(1, 2);
            removeLastOutputSegment();
        } else if (inputBuffer == "." || inputBuffer == "..") {
            inputBuffer.clear();
        } else {
            std::size_t i = 1;
            while (i < inputBuffer.size() && inputBuffer[i] != '/') ++i;
            outputBuffer.append(inputBuffer, 0, i);
            inputBuffer.erase(0, i);
        }
    }
    return std::string_view(outputBuffer);
}

} // namespace


uri resolve_reference(const uri& base, const uri& reference)
{
    CSE_INPUT_CHECK(base.scheme().has_value());

    std::string_view scheme;
    std::optional<std::string_view> authority;
    std::string pathBuffer;
    std::string_view path;
    std::optional<std::string_view> query;
    std::optional<std::string_view> fragment;

    // Canonical algorithm from RFC 3986 Sec. 5.2.2.
    if (reference.scheme().has_value()) {
        scheme = *reference.scheme();
        authority = reference.authority();
        path = remove_dot_segments(reference.path(), pathBuffer);
        query = reference.query();
    } else {
        if (reference.authority().has_value()) {
            authority = reference.authority();
            path = remove_dot_segments(reference.path(), pathBuffer);
            query = reference.query();
        } else {
            if (reference.path().empty()) {
                path = base.path();
                if (reference.query().has_value()) {
                    query = reference.query();
                } else {
                    query = base.query();
                }
            } else {
                if (reference.path().front() == '/') {
                    path = remove_dot_segments(reference.path(), pathBuffer);
                } else {
                    pathBuffer = merge(
                        base.authority().has_value(),
                        base.path(),
                        reference.path());
                    path = remove_dot_segments(pathBuffer, pathBuffer);
                }
                query = reference.query();
            }
            authority = base.authority();
        }
        scheme = *base.scheme();
    }
    fragment = reference.fragment();

    return uri(scheme, authority, path, query, fragment);
}


// =============================================================================
// functions
// =============================================================================


namespace
{
bool is_unreserved(char c) noexcept
{
    return ('A' <= c && c <= 'Z') || ('a' <= c && c <= 'z') ||
        ('0' <= c && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~';
}

bool is_in_cstring(char c, const char* s)
{
    if (s == nullptr) return false;
    while (*s != '\0' && *s != c) ++s;
    return *s != '\0';
}
} // namespace

std::string percent_encode(std::string_view string, const char* exceptions)
{
    std::ostringstream encoded;
    encoded.fill('0');
    encoded << std::hex << std::uppercase;
    for (const char c : string) {
        if (is_unreserved(c) || is_in_cstring(c, exceptions)) {
            encoded << c;
        } else {
            encoded
                << '%'
                << std::setw(2)
                << static_cast<int>(static_cast<unsigned char>(c));
        }
    }
    return encoded.str();
}


namespace
{
int hex_to_int(char hex)
{
    if (hex >= '0' && hex <= '9') {
        return hex - '0';
    } else if (hex >= 'A' && hex <= 'F') {
        return hex + 10 - 'A';
    } else if (hex >= 'a' && hex <= 'f') {
        return hex + 10 - 'a';
    } else {
        throw std::domain_error(std::string("Not a hexadecimal digit: ") + hex);
    }
}
} // namespace

std::string percent_decode(std::string_view encoded)
{
    std::string decoded;
    while (!encoded.empty()) {
        const auto i = encoded.find('%');
        if (i > encoded.size() - 3) {
            decoded += encoded;
            encoded.remove_prefix(encoded.size());
        } else {
            decoded += encoded.substr(0, i);
            decoded += static_cast<char>(
                (hex_to_int(encoded[i + 1]) << 4) | hex_to_int(encoded[i + 2]));
            encoded.remove_prefix(i + 3);
        }
    }
    return decoded;
}


uri percent_encode_uri(
    std::optional<std::string_view> scheme,
    std::optional<std::string_view> authority,
    std::string_view path,
    std::optional<std::string_view> query,
    std::optional<std::string_view> fragment)
{
    std::string s, a, p, q, f; // buffers to hold the encoded components
    if (scheme) scheme = s = percent_encode(*scheme, "+");
    if (authority) authority = a = percent_encode(*authority, "@:+");
    path = p = percent_encode(path, "/+");
    if (query) query = q = percent_encode(*query, "=&;/:+");
    if (fragment) fragment = f = percent_encode(*fragment);
    return uri(scheme, authority, path, query, fragment);
}


uri path_to_file_uri(const boost::filesystem::path& path)
{
    CSE_INPUT_CHECK(path.empty() || path.has_root_directory());

#ifdef _WIN32
    // Windows has some special rules for file URIs; better use the built-in
    // functions.
    wchar_t buffer16[INTERNET_MAX_URL_LENGTH];
    DWORD size16 = INTERNET_MAX_URL_LENGTH;
    const auto rc = UrlCreateFromPathW(path.c_str(), buffer16, &size16, 0);
    assert(rc != S_FALSE); // If `path` contains an URL, something is fishy.
    if (rc != S_OK && rc != S_FALSE) {
        throw std::system_error(rc, std::system_category());
    }

    char buffer8[2 * INTERNET_MAX_URL_LENGTH];
    const auto size8 = WideCharToMultiByte(
        CP_UTF8, WC_ERR_INVALID_CHARS,
        buffer16, size16,
        buffer8, sizeof buffer8,
        NULL, NULL);
    if (size8 <= 0) {
        throw std::system_error(GetLastError(), std::system_category());
    }
    return uri(std::string(buffer8, size8));

#else
    if (path.empty()) {
        return uri("file:");
    } else {
        return percent_encode_uri("file", std::string_view(), path.string());
    }
#endif
}


boost::filesystem::path file_uri_to_path(const uri& fileUri)
{
    CSE_INPUT_CHECK(fileUri.scheme() && *fileUri.scheme() == "file");
    CSE_INPUT_CHECK(fileUri.authority() &&
        (fileUri.authority()->empty() || *fileUri.authority() == "localhost"));

#ifdef _WIN32
    // Windows has some special rules for file URIs; better use the built-in
    // functions.
    wchar_t urlBuffer[INTERNET_MAX_URL_LENGTH];
    const auto urlSize = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS,
        fileUri.view().data(), static_cast<int>(fileUri.view().size()),
        urlBuffer, INTERNET_MAX_URL_LENGTH);
    if (urlSize <= 0) {
        throw std::system_error(GetLastError(), std::system_category());
    }
    urlBuffer[urlSize] = '\0';

    wchar_t pathBuffer[MAX_PATH];
    DWORD pathSize = MAX_PATH;
    const auto rc = PathCreateFromUrlW(urlBuffer, pathBuffer, &pathSize, 0);
    if (rc != S_OK) {
        throw std::system_error(rc, std::system_category());
    }
    return boost::filesystem::path(pathBuffer, pathBuffer + pathSize);

#else
    return boost::filesystem::path(percent_decode(fileUri.path()));
#endif
}


} // namespace cse
