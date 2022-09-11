
#include <cosim/uri.hpp>

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include <stdexcept>

using namespace cosim;


TEST_CASE("uri_parser")
{
    const uri emptyURI;
    CHECK(emptyURI == uri(""));
    CHECK(emptyURI == uri(std::nullopt, std::nullopt, ""));
    CHECK(emptyURI.view().empty());
    CHECK(!emptyURI.scheme().has_value());
    CHECK(!emptyURI.authority().has_value());
    CHECK(emptyURI.path().empty());
    CHECK(!emptyURI.query().has_value());
    CHECK(!emptyURI.fragment().has_value());
    CHECK(emptyURI.empty());

    /*not const*/ auto httpURI = uri("http://user@example.com:1234/foo/bar?q=uux#frag");
    CHECK(httpURI.view() == "http://user@example.com:1234/foo/bar?q=uux#frag");
    REQUIRE(httpURI.scheme().has_value());
    CHECK(*httpURI.scheme() == "http");
    REQUIRE(httpURI.authority().has_value());
    CHECK(*httpURI.authority() == "user@example.com:1234");
    CHECK(httpURI.path() == "/foo/bar");
    REQUIRE(httpURI.query().has_value());
    CHECK(*httpURI.query() == "q=uux");
    REQUIRE(httpURI.fragment().has_value());
    CHECK(*httpURI.fragment() == "frag");
    CHECK(!httpURI.empty());

    const auto httpURI2 = uri("http", "user@example.com:1234", "/foo/bar", "q=uux", "frag");
    CHECK(httpURI2.view() == "http://user@example.com:1234/foo/bar?q=uux#frag");
    REQUIRE(httpURI2.scheme().has_value());
    CHECK(*httpURI2.scheme() == "http");
    REQUIRE(httpURI2.authority().has_value());
    CHECK(*httpURI2.authority() == "user@example.com:1234");
    CHECK(httpURI2.path() == "/foo/bar");
    REQUIRE(httpURI2.query().has_value());
    CHECK(*httpURI2.query() == "q=uux");
    REQUIRE(httpURI2.fragment().has_value());
    CHECK(*httpURI2.fragment() == "frag");
    CHECK(!httpURI2.empty());

    const auto fileURI = uri("file:///foo/bar#frag?q=uux");
    CHECK(fileURI.view() == "file:///foo/bar#frag?q=uux");
    REQUIRE(fileURI.scheme().has_value());
    CHECK(*fileURI.scheme() == "file");
    REQUIRE(fileURI.authority().has_value());
    CHECK(fileURI.authority()->empty());
    CHECK(fileURI.path() == "/foo/bar");
    CHECK(!fileURI.query().has_value());
    REQUIRE(fileURI.fragment().has_value());
    CHECK(*fileURI.fragment() == "frag?q=uux");
    CHECK(!fileURI.empty());

    const auto mailtoURI = uri("mailto:user@example.com");
    CHECK(mailtoURI.view() == "mailto:user@example.com");
    REQUIRE(mailtoURI.scheme().has_value());
    CHECK(*mailtoURI.scheme() == "mailto");
    CHECK(!mailtoURI.authority().has_value());
    CHECK(mailtoURI.path() == "user@example.com");
    CHECK(!mailtoURI.query().has_value());
    CHECK(!mailtoURI.fragment().has_value());
    CHECK(!mailtoURI.empty());

    const auto urnURI = uri("urn:foo:bar:baz");
    CHECK(urnURI.view() == "urn:foo:bar:baz");
    REQUIRE(urnURI.scheme().has_value());
    CHECK(*urnURI.scheme() == "urn");
    CHECK(!urnURI.authority().has_value());
    CHECK(urnURI.path() == "foo:bar:baz");
    CHECK(!urnURI.query().has_value());
    CHECK(!urnURI.fragment().has_value());
    CHECK(!urnURI.empty());
}


TEST_CASE("uri_copy_and_move")
{
    auto orig = uri("http://user@example.com:1234/foo/bar?q=uux#frag");
    const auto copy = orig;
    const auto move = std::move(orig);
    orig = uri();

    REQUIRE(copy.scheme().has_value());
    CHECK(*copy.scheme() == "http");
    REQUIRE(copy.authority().has_value());
    CHECK(*copy.authority() == "user@example.com:1234");
    CHECK(copy.path() == "/foo/bar");
    REQUIRE(copy.query().has_value());
    CHECK(*copy.query() == "q=uux");
    REQUIRE(copy.fragment().has_value());
    CHECK(*copy.fragment() == "frag");

    REQUIRE(move.scheme().has_value());
    CHECK(*move.scheme() == "http");
    REQUIRE(move.authority().has_value());
    CHECK(*move.authority() == "user@example.com:1234");
    CHECK(move.path() == "/foo/bar");
    REQUIRE(move.query().has_value());
    CHECK(*move.query() == "q=uux");
    REQUIRE(move.fragment().has_value());
    CHECK(*move.fragment() == "frag");

    // Special case: Short strings which may be affected by the small-string
    // optimisation (see issue #361)
    auto small = uri("x");
    const auto smallCopy = small;
    const auto smallMove = std::move(small);
    small = uri();

    CHECK(smallCopy.path() == "x");
    CHECK(smallMove.path() == "x");
}


TEST_CASE("uri_comparison")
{
    const auto httpURI = uri("http://user@example.com:1234/foo/bar?q=uux#frag");
    const auto fileURI = uri("file:///foo/bar#frag?q=uux");

    CHECK(httpURI == httpURI);
    CHECK(httpURI == "http://user@example.com:1234/foo/bar?q=uux#frag");
    CHECK("http://user@example.com:1234/foo/bar?q=uux#frag" == httpURI);
    CHECK(httpURI != fileURI);
    CHECK(fileURI != httpURI);
    CHECK(fileURI != "http://user@example.com:1234/foo/bar?q=uux#frag");
    CHECK("http://user@example.com:1234/foo/bar?q=uux#frag" != fileURI);
}


// URI reference resolution examples from RFC 3986
TEST_CASE("uri_resolution")
{
    const auto baseURI = uri("http://a/b/c/d;p?q");
    CHECK(resolve_reference(baseURI, "g:h") == "g:h");
    CHECK(resolve_reference(baseURI, "g") == "http://a/b/c/g");
    CHECK(resolve_reference(baseURI, "./g") == "http://a/b/c/g");
    CHECK(resolve_reference(baseURI, "g/") == "http://a/b/c/g/");
    CHECK(resolve_reference(baseURI, "/g") == "http://a/g");
    CHECK(resolve_reference(baseURI, "//g") == "http://g");
    CHECK(resolve_reference(baseURI, "?y") == "http://a/b/c/d;p?y");
    CHECK(resolve_reference(baseURI, "g?y") == "http://a/b/c/g?y");
    CHECK(resolve_reference(baseURI, "#s") == "http://a/b/c/d;p?q#s");
    CHECK(resolve_reference(baseURI, "g#s") == "http://a/b/c/g#s");
    CHECK(resolve_reference(baseURI, "g?y#s") == "http://a/b/c/g?y#s");
    CHECK(resolve_reference(baseURI, ";x") == "http://a/b/c/;x");
    CHECK(resolve_reference(baseURI, "g;x") == "http://a/b/c/g;x");
    CHECK(resolve_reference(baseURI, "g;x?y#s") == "http://a/b/c/g;x?y#s");
    CHECK(resolve_reference(baseURI, "") == "http://a/b/c/d;p?q");
    CHECK(resolve_reference(baseURI, ".") == "http://a/b/c/");
    CHECK(resolve_reference(baseURI, "./") == "http://a/b/c/");
    CHECK(resolve_reference(baseURI, "..") == "http://a/b/");
    CHECK(resolve_reference(baseURI, "../") == "http://a/b/");
    CHECK(resolve_reference(baseURI, "../g") == "http://a/b/g");
    CHECK(resolve_reference(baseURI, "../..") == "http://a/");
    CHECK(resolve_reference(baseURI, "../../") == "http://a/");
    CHECK(resolve_reference(baseURI, "../../g") == "http://a/g");

    CHECK(resolve_reference(baseURI, "../../../g") == "http://a/g");
    CHECK(resolve_reference(baseURI, "../../../../g") == "http://a/g");

    CHECK(resolve_reference(baseURI, "/./g") == "http://a/g");
    CHECK(resolve_reference(baseURI, "/../g") == "http://a/g");
    CHECK(resolve_reference(baseURI, "g.") == "http://a/b/c/g.");
    CHECK(resolve_reference(baseURI, ".g") == "http://a/b/c/.g");
    CHECK(resolve_reference(baseURI, "g..") == "http://a/b/c/g..");
    CHECK(resolve_reference(baseURI, "..g") == "http://a/b/c/..g");

    CHECK(resolve_reference(baseURI, "g?y/./x") == "http://a/b/c/g?y/./x");
    CHECK(resolve_reference(baseURI, "g?y/../x") == "http://a/b/c/g?y/../x");
    CHECK(resolve_reference(baseURI, "g#s/./x") == "http://a/b/c/g#s/./x");
    CHECK(resolve_reference(baseURI, "g#s/../x") == "http://a/b/c/g#s/../x");

    CHECK(resolve_reference(baseURI, "http:g") == "http:g");
}


TEST_CASE("percent_encoding")
{
    CHECK(percent_encode(" foo*/123;bar%") == "%20foo%2A%2F123%3Bbar%25");
    CHECK(percent_encode(" foo*/123;bar%", "/;") == "%20foo%2A/123;bar%25");

    CHECK(percent_decode("%20foo%2A%2F123%3Bbar%25") == " foo*/123;bar%");
    CHECK(percent_decode("%20foo%2a%2f123%3bbar%25") == " foo*/123;bar%");
    CHECK_THROWS_AS(percent_decode("%G0"), std::domain_error);
    CHECK_THROWS_AS(percent_decode("%0G"), std::domain_error);

    CHECK(percent_encode_uri("http", "user name@10.0.0.1:passwd", "/foo/bar baz", "q1=foo bar&q2=baz;q3", "foo bar") == "http://user%20name@10.0.0.1:passwd/foo/bar%20baz?q1=foo%20bar&q2=baz;q3#foo%20bar");
}


TEST_CASE("file_uri_conversions")
{
    // From path to URI
    CHECK(path_to_file_uri("/foo bar/baz") == "file:///foo%20bar/baz");
    CHECK(path_to_file_uri(cosim::filesystem::path()) == "file:");
#ifdef _WIN32
    CHECK(path_to_file_uri("\\foo bar\\baz") == "file:///foo%20bar/baz");
    CHECK(path_to_file_uri("/foo bar/baz") == "file:///foo%20bar/baz");
    CHECK(path_to_file_uri("c:\\foo bar\\baz") == "file:///c:/foo%20bar/baz");
#endif

    // From URI to path
#ifdef _WIN32
    CHECK(file_uri_to_path("file:///foo%20bar/baz") == "\\foo bar\\baz");
    CHECK(file_uri_to_path("file:///c:/foo%20bar/baz") == "c:\\foo bar\\baz");
    CHECK(file_uri_to_path("file://localhost/foo%20bar/baz") == "\\foo bar\\baz");
    CHECK(file_uri_to_path("file://localhost/c:/foo%20bar/baz") == "c:\\foo bar\\baz");
#else
    CHECK(file_uri_to_path("file:///foo%20bar/baz") == "/foo bar/baz");
    CHECK(file_uri_to_path("file:///c:/foo%20bar/baz") == "/c:/foo bar/baz");
    CHECK(file_uri_to_path("file://localhost/foo%20bar/baz") == "/foo bar/baz");
    CHECK(file_uri_to_path("file://localhost/c:/foo%20bar/baz") == "/c:/foo bar/baz");
#endif
    CHECK_THROWS_AS(file_uri_to_path("http://foo/bar"), std::invalid_argument);
    CHECK_THROWS_AS(file_uri_to_path("file://foo/bar"), std::invalid_argument);
}
