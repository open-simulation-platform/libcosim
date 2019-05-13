#include "cse/uri.hpp"

#include "cse/error.hpp"

#include <cassert>
#include <cctype>
#include <stdexcept>


namespace cse
{

// =============================================================================
// class uri
// =============================================================================

namespace
{

bool is_scheme_char(char c) noexcept
{
    return std::isalnum(c) || c == '+' || c == '-' || c == '.';
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

} // namespace


uri::uri() noexcept = default;


/*implicit*/ uri::uri(std::string string)
    : data_(std::move(string))
{
    auto view = std::string_view(data_);
    scheme_ = consume_scheme(view);
    authority_ = consume_authority(view);
    path_ = consume_path(view);
    query_ = consume_query(view);
    fragment_ = consume_fragment(view);
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
        scheme_ = view.substr(0, scheme->size());
        view.remove_prefix(scheme->size() + 1);
    }
    if (authority) {
        data_ += "//";
        data_ += *authority;
        authority_ = view.substr(2, authority->size());
        view.remove_prefix(2 + authority->size());
    }
    data_ += path;
    path_ = view.substr(0, path.size());
    view.remove_prefix(path.size());
    if (query) {
        data_ += '?';
        data_ += *query;
        query_ = view.substr(1, query->size());
        view.remove_prefix(1 + query->size());
    }
    if (fragment) {
        data_ += '#';
        data_ += *fragment;
        fragment_ = view.substr(1, fragment->size());
        view.remove_prefix(1 + fragment->size());
    }
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
// misc
// =============================================================================

uri make_file_uri(const boost::filesystem::path& path)
{
#ifdef _WIN32
    if (path.empty()) return uri("file", std::string_view(), std::string_view());
    const auto firstChar = path.native().front();
    if (firstChar == '/' || firstChar == '\\') {
        return uri("file", std::string_view(), path.generic_string());
    } else {
        return uri("file", std::string_view(), '/' + path.generic_string());
    }
#else
    return uri("file", std::string_view(), path.native());
#endif
}

} // namespace cse
