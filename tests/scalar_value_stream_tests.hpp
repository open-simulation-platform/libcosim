#ifndef COSIM_SCALAR_VALUE_STREAM_TESTS_HPP
#define COSIM_SCALAR_VALUE_STREAM_TESTS_HPP

#include <cosim/model_description.hpp>

#include <boost/test/unit_test.hpp>

#include <iomanip>
#include <locale>
#include <sstream>


namespace
{

template<typename T>
void check_scalar_stream_roundtrip(T expected, const std::string& text)
{
    const cosim::scalar_value original(expected);
    std::ostringstream output;
    output << original;
    BOOST_CHECK_EQUAL(output.str(), text);

    cosim::scalar_value result(T{});
    std::istringstream input(text);
    input >> result;
    BOOST_CHECK(!input.fail());
    BOOST_CHECK(std::holds_alternative<T>(result));
    BOOST_CHECK(std::get<T>(result) == expected);
}

template<typename T>
void check_byte_input(const std::string& text, T initial, int expected, bool fails)
{
    cosim::scalar_value value(initial);
    std::istringstream input(text);
    input >> value;
    BOOST_CHECK_EQUAL(input.fail(), fails);
    BOOST_CHECK(std::holds_alternative<T>(value));
    BOOST_CHECK_EQUAL(static_cast<int>(std::get<T>(value)), expected);
}

class grouped_digits : public std::numpunct<char>
{
    char do_thousands_sep() const override { return '_'; }
    std::string do_grouping() const override { return "\3"; }
};

} // namespace


BOOST_AUTO_TEST_CASE(scalar_streams_preserve_alternatives)
{
    check_scalar_stream_roundtrip<double>(1.5, "1.5");
    check_scalar_stream_roundtrip<int>(-123, "-123");
    check_scalar_stream_roundtrip<bool>(true, "1");
    check_scalar_stream_roundtrip<std::string>("hello", "hello");
    check_scalar_stream_roundtrip<cosim::enumeration_value>({-9}, "-9");
    check_scalar_stream_roundtrip<float>(2.5F, "2.5");
    check_scalar_stream_roundtrip<std::int8_t>(-128, "-128");
    check_scalar_stream_roundtrip<std::int8_t>(127, "127");
    check_scalar_stream_roundtrip<std::int8_t>(42, "42");
    check_scalar_stream_roundtrip<std::uint8_t>(0, "0");
    check_scalar_stream_roundtrip<std::uint8_t>(255, "255");
    check_scalar_stream_roundtrip<std::uint8_t>(42, "42");
    check_scalar_stream_roundtrip<std::int16_t>(-32000, "-32000");
    check_scalar_stream_roundtrip<std::uint16_t>(65000, "65000");
    check_scalar_stream_roundtrip<std::uint32_t>(4000000000U, "4000000000");
    check_scalar_stream_roundtrip<std::int64_t>(-5000000000LL, "-5000000000");
    check_scalar_stream_roundtrip<std::uint64_t>(5000000000ULL, "5000000000");

    cosim::scalar_value first(std::int8_t{42});
    cosim::scalar_value second(std::uint8_t{255});
    std::stringstream chained;
    chained << first << ' ' << second;
    first = std::int8_t{0};
    second = std::uint8_t{0};
    chained >> first >> second;
    BOOST_CHECK_EQUAL(static_cast<int>(std::get<std::int8_t>(first)), 42);
    BOOST_CHECK_EQUAL(static_cast<int>(std::get<std::uint8_t>(second)), 255);

    const cosim::scalar_value binary(cosim::binary_value{{0x00, 0x0f, 0xa5, 0xff}});
    std::ostringstream binaryOutput;
    binaryOutput << binary;
    BOOST_CHECK_EQUAL(binaryOutput.str(), "000fa5ff");
}


BOOST_AUTO_TEST_CASE(scalar_stream_byte_range_and_invalid_input)
{
    check_byte_input<std::int8_t>(" -128 ", 3, -128, false);
    check_byte_input<std::int8_t>("127", 3, 127, false);
    check_byte_input<std::int8_t>("-129", 3, -128, true);
    check_byte_input<std::int8_t>("128", 3, 127, true);
    check_byte_input<std::uint8_t>("0", 3, 0, false);
    check_byte_input<std::uint8_t>("255", 3, 255, false);
    check_byte_input<std::uint8_t>("-1", 3, 0, true);
    check_byte_input<std::uint8_t>("256", 3, 255, true);
    check_byte_input<std::int8_t>("-999999999999999999999999999999", 3, -128, true);
    check_byte_input<std::int8_t>("999999999999999999999999999999", 3, 127, true);
    check_byte_input<std::uint8_t>("-999999999999999999999999999999", 3, 0, true);
    check_byte_input<std::uint8_t>("999999999999999999999999999999", 3, 255, true);
    check_byte_input<std::int8_t>("not-a-number", 42, 42, true);
    check_byte_input<std::uint8_t>("", 42, 42, true);
    check_byte_input<std::int8_t>("-", 42, 42, true);
}


BOOST_AUTO_TEST_CASE(scalar_stream_flags_locale_and_binary_failure)
{
    cosim::scalar_value byte(std::uint8_t{255});
    std::ostringstream formatted;
    formatted << std::showbase << std::uppercase << std::hex << byte;
    BOOST_CHECK_EQUAL(formatted.str(), "0XFF");

    std::istringstream hexadecimal("0xff");
    byte = std::uint8_t{0};
    hexadecimal >> std::hex >> byte;
    BOOST_CHECK(!hexadecimal.fail());
    BOOST_CHECK_EQUAL(static_cast<int>(std::get<std::uint8_t>(byte)), 255);
    std::istringstream hexadecimalOverflow("0x100");
    hexadecimalOverflow >> std::hex >> byte;
    BOOST_CHECK(hexadecimalOverflow.fail());
    BOOST_CHECK_EQUAL(static_cast<int>(std::get<std::uint8_t>(byte)), 255);

    std::istringstream octal("0377");
    byte = std::uint8_t{0};
    octal >> std::setbase(0) >> byte;
    BOOST_CHECK(!octal.fail());
    BOOST_CHECK_EQUAL(static_cast<int>(std::get<std::uint8_t>(byte)), 255);

    std::istringstream grouped("0_127");
    grouped.imbue(std::locale(std::locale::classic(), new grouped_digits));
    cosim::scalar_value signedByte(std::int8_t{0});
    grouped >> signedByte;
    BOOST_CHECK(!grouped.fail());
    BOOST_CHECK_EQUAL(static_cast<int>(std::get<std::int8_t>(signedByte)), 127);

    cosim::scalar_value boolean(false);
    std::stringstream boolStream;
    boolStream << std::boolalpha << cosim::scalar_value(true);
    BOOST_CHECK_EQUAL(boolStream.str(), "true");
    boolStream >> std::boolalpha >> boolean;
    BOOST_CHECK(std::get<bool>(boolean));

    cosim::scalar_value text(std::string("old"));
    std::istringstream words("first second");
    words >> text;
    BOOST_CHECK_EQUAL(std::get<std::string>(text), "first");
    BOOST_CHECK_EQUAL(words.peek(), ' ');

    const cosim::binary_value initial{{0x12, 0x34}};
    cosim::scalar_value binary(initial);
    std::istringstream binaryInput("abcd");
    binaryInput >> binary;
    BOOST_CHECK(binaryInput.fail());
    BOOST_CHECK(std::get<cosim::binary_value>(binary) == initial);
    binaryInput.clear();
    BOOST_CHECK_EQUAL(binaryInput.peek(), 'a');
}


BOOST_AUTO_TEST_CASE(scalar_stream_exception_masks)
{
    cosim::scalar_value byte(std::int8_t{7});
    std::istringstream range("128");
    range.exceptions(std::ios_base::failbit);
    BOOST_CHECK_THROW(range >> byte, std::ios_base::failure);
    BOOST_CHECK(range.fail());
    BOOST_CHECK_EQUAL(static_cast<int>(std::get<std::int8_t>(byte)), 127);

    byte = std::int8_t{7};
    std::istringstream huge("-999999999999999999999999999999");
    huge.exceptions(std::ios_base::failbit);
    BOOST_CHECK_THROW(huge >> byte, std::ios_base::failure);
    BOOST_CHECK(huge.fail());
    BOOST_CHECK_EQUAL(static_cast<int>(std::get<std::int8_t>(byte)), -128);

    byte = std::int8_t{7};
    std::istringstream invalid("x");
    invalid.exceptions(std::ios_base::failbit);
    BOOST_CHECK_THROW(invalid >> byte, std::ios_base::failure);
    BOOST_CHECK_EQUAL(static_cast<int>(std::get<std::int8_t>(byte)), 7);

    byte = std::int8_t{7};
    std::istringstream eof("42");
    eof.exceptions(std::ios_base::eofbit);
    BOOST_CHECK_THROW(eof >> byte, std::ios_base::failure);
    BOOST_CHECK(!eof.fail());
    BOOST_CHECK_EQUAL(static_cast<int>(std::get<std::int8_t>(byte)), 42);

    byte = std::int8_t{7};
    std::istringstream eofOverflow("128");
    eofOverflow.exceptions(std::ios_base::eofbit);
    BOOST_CHECK_THROW(eofOverflow >> byte, std::ios_base::failure);
    BOOST_CHECK(eofOverflow.fail());
    BOOST_CHECK_EQUAL(static_cast<int>(std::get<std::int8_t>(byte)), 127);

    const cosim::binary_value initial{{0xab}};
    cosim::scalar_value binary(initial);
    std::istringstream unsupported("ab");
    unsupported.exceptions(std::ios_base::failbit);
    BOOST_CHECK_THROW(unsupported >> binary, std::ios_base::failure);
    BOOST_CHECK(std::get<cosim::binary_value>(binary) == initial);
}

#endif
