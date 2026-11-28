/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_XML_VALUE_HPP
#define COSIM_XML_VALUE_HPP

#include "cosim/model_description.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>
#include <variant> // IWYU pragma: keep
#include <vector>

namespace cosim::detail
{

inline std::optional<variable_type> xml_variable_type(std::string_view name)
{
    using V = variable_type;
    constexpr std::array<std::pair<std::string_view, V>, 17> types{
        {
            {"Real", V::real},
            {"Float64", V::real},
            {"Float32", V::float32},
            {"Integer", V::integer},
            {"Int32", V::integer},
            {"Int8", V::int8},
            {"UInt8", V::uint8},
            {"Int16", V::int16},
            {"UInt16", V::uint16},
            {"UInt32", V::uint32},
            {"Int64", V::int64},
            {"UInt64", V::uint64},
            {"Boolean", V::boolean},
            {"String", V::string},
            {"Enumeration", V::enumeration},
            {"Binary", V::binary},
            {"Clock", V::clock},
        },
    };
    for (const auto& [key, type] : types) {
        if (key == name) {
            return type;
        }
    }
    return std::nullopt;
}

inline std::string_view trim_xml_whitespace(std::string_view text)
{
    constexpr std::string_view whitespace = " \t\r\n";
    const auto first = text.find_first_not_of(whitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(whitespace) - first + 1);
}

inline std::string_view strip_xml_number_plus(std::string_view text)
{
    if (!text.empty() && text.front() == '+') {
        text.remove_prefix(1);
        if (!text.empty() && text.front() == '-') {
            return {};
        }
    }
    return text;
}

template<typename T>
T parse_xml_integer(std::string_view text)
{
    text = strip_xml_number_plus(text);
    if (text.empty()) {
        throw std::invalid_argument("Missing XML integer digits");
    }

    if constexpr (std::is_unsigned_v<T>) {
        // The XML Schema nonnegative constraint is on the value, not the sign.
        if (text.size() > 1 && text.front() == '-' &&
            text.find_first_not_of('0', 1) == std::string_view::npos) {
            return 0;
        }
    }

    T value{};
    const auto* const end = text.data() + text.size(); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): from_chars requires a pointer range.
    const auto result = std::from_chars(
        text.data(), end, // NOLINT(bugprone-suspicious-stringview-data-usage)
        value);
    if (result.ptr == end && result.ec == std::errc{}) {
        return value;
    }
    if (result.ptr == end && result.ec == std::errc::result_out_of_range) {
        throw std::out_of_range("XML integer does not fit the variable type");
    }
    throw std::invalid_argument("Invalid XML integer literal");
}

template<typename T>
T parse_xml_float(std::string_view text)
{
    if (text == "INF") {
        return std::numeric_limits<T>::infinity();
    }
    if (text == "-INF") {
        return -std::numeric_limits<T>::infinity();
    }
    if (text == "NaN") {
        return std::numeric_limits<T>::quiet_NaN();
    }

    const auto number = strip_xml_number_plus(text);
    auto digits = number;
    if (!digits.empty() && digits.front() == '-') {
        digits.remove_prefix(1);
    }
    // from_chars also accepts non-XML spellings such as "infinity" and "nan".
    if (digits.empty() || (digits.front() != '.' && (digits.front() < '0' || digits.front() > '9'))) {
        throw std::invalid_argument("Invalid XML floating-point literal");
    }

    T value{};
    const auto* const end = number.data() + number.size(); // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic): from_chars requires a pointer range.
    const auto result = std::from_chars(
        number.data(), end, // NOLINT(bugprone-suspicious-stringview-data-usage)
        value);
    if (result.ptr == end && result.ec == std::errc{}) {
        return value;
    }
    if (result.ptr == end && result.ec == std::errc::result_out_of_range) {
        throw std::out_of_range(
            "XML floating-point value does not fit the variable type");
    }
    throw std::invalid_argument("Invalid XML floating-point literal");
}

inline binary_value parse_xml_hex_binary(std::string_view text)
{
    constexpr unsigned hex_base = 16;
    const auto digit = [](char character) -> unsigned {
        constexpr unsigned hex_letter_offset = 10;
        if (character >= '0' && character <= '9') {
            return character - '0';
        }
        if (character >= 'A' && character <= 'F') {
            return character - 'A' + hex_letter_offset;
        }
        if (character >= 'a' && character <= 'f') {
            return character - 'a' + hex_letter_offset;
        }
        throw std::invalid_argument(
            "Invalid hexadecimal digit in XML Binary value");
    };

    if (text.size() % 2 != 0) {
        throw std::invalid_argument(
            "XML Binary values require an even number of hexadecimal digits");
    }
    binary_value value;
    value.data.reserve(text.size() / 2);
    while (!text.empty()) {
        const auto high = digit(text.front());
        text.remove_prefix(1);
        const auto low = digit(text.front());
        text.remove_prefix(1);
        value.data.push_back(static_cast<std::uint8_t>((hex_base * high) + low));
    }
    return value;
}

/// Parses an XML scalar literal in its exact storage type. String whitespace
/// is preserved; symbolic Enumeration items must be resolved by the caller.
inline scalar_value parse_xml_scalar_value(variable_type type,
    std::string_view text)
{
    if (type == variable_type::string) {
        return std::string(text);
    }
    text = trim_xml_whitespace(text);

    switch (type) {
        case variable_type::real:
            return parse_xml_float<double>(text);
        case variable_type::integer:
            return parse_xml_integer<int>(text);
        case variable_type::float32:
            return parse_xml_float<float>(text);
        case variable_type::int8:
            return parse_xml_integer<std::int8_t>(text);
        case variable_type::uint8:
            return parse_xml_integer<std::uint8_t>(text);
        case variable_type::int16:
            return parse_xml_integer<std::int16_t>(text);
        case variable_type::uint16:
            return parse_xml_integer<std::uint16_t>(text);
        case variable_type::uint32:
            return parse_xml_integer<std::uint32_t>(text);
        case variable_type::int64:
            return parse_xml_integer<std::int64_t>(text);
        case variable_type::uint64:
            return parse_xml_integer<std::uint64_t>(text);
        case variable_type::enumeration:
            return enumeration_value{parse_xml_integer<std::int64_t>(text)};
        case variable_type::boolean:
            if (text == "true" || text == "1") {
                return true;
            }
            if (text == "false" || text == "0") {
                return false;
            }
            throw std::invalid_argument("Invalid XML Boolean literal");
        case variable_type::binary:
            return parse_xml_hex_binary(text);
        case variable_type::clock:
            throw std::invalid_argument("Clock has no XML data value");
        case variable_type::string:
            break; // Handled before whitespace trimming.
    }
    throw std::invalid_argument("Unsupported XML value type");
}

inline bool xml_value_has_elements(variable_type type)
{
    return type == variable_type::string || type == variable_type::binary ||
        type == variable_type::enumeration;
}

template<typename ParseScalar, std::size_t Index = 0>
variable_value make_xml_variable_value(
    variable_type type, const std::vector<std::string_view>& items,
    std::vector<std::uint64_t> shape, const ParseScalar& parseScalar) // NOLINT(performance-unnecessary-value-param): Shape ownership moves through the recursive alternatives.
{
    if constexpr (Index == std::variant_size_v<variable_value_storage>) {
        throw std::invalid_argument("Unsupported XML value type");
    } else {
        using Storage = std::variant_alternative_t<Index, variable_value_storage>;
        if (type == Storage::type) {
            Storage storage;
            storage.values.reserve(items.size());
            for (const auto item : items) {
                storage.values.push_back(
                    std::get<typename Storage::value_type>(parseScalar(type, item)));
            }
            return variable_value(std::move(storage), std::move(shape));
        }
        return make_xml_variable_value<ParseScalar, Index + 1>(
            type, items, std::move(shape), parseScalar);
    }
}

template<typename ParseScalar = decltype(&parse_xml_scalar_value)>
variable_value
parse_xml_value(variable_type type, std::optional<std::string_view> attribute,
    std::vector<std::string_view> items,
    std::vector<std::uint64_t> shape,
    const ParseScalar& parseScalar = &parse_xml_scalar_value)
{
    if (attribute && !items.empty()) {
        throw std::invalid_argument(
            "Cannot combine a value attribute with Value elements");
    }
    if (attribute) {
        if (shape.empty() || xml_value_has_elements(type)) {
            items.push_back(*attribute);
        } else {
            auto text = trim_xml_whitespace(*attribute);
            while (!text.empty()) {
                const auto next = text.find_first_of(" \t\r\n");
                items.push_back(text.substr(0, next));
                if (next == std::string_view::npos) {
                    break;
                }
                text = trim_xml_whitespace(text.substr(next));
            }
        }
    }
    if (items.size() != flat_size(shape)) {
        throw std::invalid_argument(
            "XML value element count does not match the declared shape");
    }
    return make_xml_variable_value(type, items, std::move(shape), parseScalar);
}

} // namespace cosim::detail
#endif
