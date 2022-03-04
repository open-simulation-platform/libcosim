#define BOOST_TEST_MODULE uri.hpp unittests
#include <cosim/uri.hpp>

#include <boost/test/unit_test.hpp>

#include <stdexcept>

using namespace cosim;


BOOST_AUTO_TEST_CASE(uri_parser)
{
    const uri emptyURI;
    BOOST_TEST(emptyURI == uri(""));
    BOOST_TEST(emptyURI == uri(std::nullopt, std::nullopt, ""));
    BOOST_TEST(emptyURI.view().empty());
    BOOST_TEST(!emptyURI.scheme().has_value());
    BOOST_TEST(!emptyURI.authority().has_value());
    BOOST_TEST(emptyURI.path().empty());
    BOOST_TEST(!emptyURI.query().has_value());
    BOOST_TEST(!emptyURI.fragment().has_value());
    BOOST_TEST(emptyURI.empty());

    /*not const*/ auto httpURI = uri("http://user@example.com:1234/foo/bar?q=uux#frag");
    BOOST_TEST(httpURI.view() == "http://user@example.com:1234/foo/bar?q=uux#frag");
    BOOST_REQUIRE(httpURI.scheme().has_value());
    BOOST_TEST(*httpURI.scheme() == "http");
    BOOST_REQUIRE(httpURI.authority().has_value());
    BOOST_TEST(*httpURI.authority() == "user@example.com:1234");
    BOOST_TEST(httpURI.path() == "/foo/bar");
    BOOST_REQUIRE(httpURI.query().has_value());
    BOOST_TEST(*httpURI.query() == "q=uux");
    BOOST_REQUIRE(httpURI.fragment().has_value());
    BOOST_TEST(*httpURI.fragment() == "frag");
    BOOST_TEST(!httpURI.empty());

    const auto httpURI2 = uri("http", "user@example.com:1234", "/foo/bar", "q=uux", "frag");
    BOOST_TEST(httpURI2.view() == "http://user@example.com:1234/foo/bar?q=uux#frag");
    BOOST_REQUIRE(httpURI2.scheme().has_value());
    BOOST_TEST(*httpURI2.scheme() == "http");
    BOOST_REQUIRE(httpURI2.authority().has_value());
    BOOST_TEST(*httpURI2.authority() == "user@example.com:1234");
    BOOST_TEST(httpURI2.path() == "/foo/bar");
    BOOST_REQUIRE(httpURI2.query().has_value());
    BOOST_TEST(*httpURI2.query() == "q=uux");
    BOOST_REQUIRE(httpURI2.fragment().has_value());
    BOOST_TEST(*httpURI2.fragment() == "frag");
    BOOST_TEST(!httpURI2.empty());

    const auto fileURI = uri("file:///foo/bar#frag?q=uux");
    BOOST_TEST(fileURI.view() == "file:///foo/bar#frag?q=uux");
    BOOST_REQUIRE(fileURI.scheme().has_value());
    BOOST_TEST(*fileURI.scheme() == "file");
    BOOST_REQUIRE(fileURI.authority().has_value());
    BOOST_TEST(fileURI.authority()->empty());
    BOOST_TEST(fileURI.path() == "/foo/bar");
    BOOST_TEST(!fileURI.query().has_value());
    BOOST_REQUIRE(fileURI.fragment().has_value());
    BOOST_TEST(*fileURI.fragment() == "frag?q=uux");
    BOOST_TEST(!fileURI.empty());

    const auto mailtoURI = uri("mailto:user@example.com");
    BOOST_TEST(mailtoURI.view() == "mailto:user@example.com");
    BOOST_REQUIRE(mailtoURI.scheme().has_value());
    BOOST_TEST(*mailtoURI.scheme() == "mailto");
    BOOST_TEST(!mailtoURI.authority().has_value());
    BOOST_TEST(mailtoURI.path() == "user@example.com");
    BOOST_TEST(!mailtoURI.query().has_value());
    BOOST_TEST(!mailtoURI.fragment().has_value());
    BOOST_TEST(!mailtoURI.empty());

    const auto urnURI = uri("urn:foo:bar:baz");
    BOOST_TEST(urnURI.view() == "urn:foo:bar:baz");
    BOOST_REQUIRE(urnURI.scheme().has_value());
    BOOST_TEST(*urnURI.scheme() == "urn");
    BOOST_TEST(!urnURI.authority().has_value());
    BOOST_TEST(urnURI.path() == "foo:bar:baz");
    BOOST_TEST(!urnURI.query().has_value());
    BOOST_TEST(!urnURI.fragment().has_value());
    BOOST_TEST(!urnURI.empty());
}


BOOST_AUTO_TEST_CASE(uri_copy_and_move)
{
    auto orig = uri("http://user@example.com:1234/foo/bar?q=uux#frag");
    const auto copy = orig;
    const auto move = std::move(orig);
    orig = uri();

    BOOST_REQUIRE(copy.scheme().has_value());
    BOOST_TEST(*copy.scheme() == "http");
    BOOST_REQUIRE(copy.authority().has_value());
    BOOST_TEST(*copy.authority() == "user@example.com:1234");
    BOOST_TEST(copy.path() == "/foo/bar");
    BOOST_REQUIRE(copy.query().has_value());
    BOOST_TEST(*copy.query() == "q=uux");
    BOOST_REQUIRE(copy.fragment().has_value());
    BOOST_TEST(*copy.fragment() == "frag");

    BOOST_REQUIRE(move.scheme().has_value());
    BOOST_TEST(*move.scheme() == "http");
    BOOST_REQUIRE(move.authority().has_value());
    BOOST_TEST(*move.authority() == "user@example.com:1234");
    BOOST_TEST(move.path() == "/foo/bar");
    BOOST_REQUIRE(move.query().has_value());
    BOOST_TEST(*move.query() == "q=uux");
    BOOST_REQUIRE(move.fragment().has_value());
    BOOST_TEST(*move.fragment() == "frag");

    // Special case: Short strings which may be affected by the small-string
    // optimisation (see issue #361)
    auto small = uri("x");
    const auto smallCopy = small;
    const auto smallMove = std::move(small);
    small = uri();

    BOOST_TEST(smallCopy.path() == "x");
    BOOST_TEST(smallMove.path() == "x");
}


BOOST_AUTO_TEST_CASE(uri_comparison)
{
    const auto httpURI = uri("http://user@example.com:1234/foo/bar?q=uux#frag");
    const auto fileURI = uri("file:///foo/bar#frag?q=uux");

    BOOST_TEST(httpURI == httpURI);
    BOOST_TEST(httpURI == "http://user@example.com:1234/foo/bar?q=uux#frag");
    BOOST_TEST("http://user@example.com:1234/foo/bar?q=uux#frag" == httpURI);
    BOOST_TEST(httpURI != fileURI);
    BOOST_TEST(fileURI != httpURI);
    BOOST_TEST(fileURI != "http://user@example.com:1234/foo/bar?q=uux#frag");
    BOOST_TEST("http://user@example.com:1234/foo/bar?q=uux#frag" != fileURI);
}


// URI reference resolution examples from RFC 3986
BOOST_AUTO_TEST_CASE(uri_resolution)
{
    const auto baseURI = uri("http://a/b/c/d;p?q");
    BOOST_TEST(resolve_reference(baseURI, "g:h") == "g:h");
    BOOST_TEST(resolve_reference(baseURI, "g") == "http://a/b/c/g");
    BOOST_TEST(resolve_reference(baseURI, "./g") == "http://a/b/c/g");
    BOOST_TEST(resolve_reference(baseURI, "g/") == "http://a/b/c/g/");
    BOOST_TEST(resolve_reference(baseURI, "/g") == "http://a/g");
    BOOST_TEST(resolve_reference(baseURI, "//g") == "http://g");
    BOOST_TEST(resolve_reference(baseURI, "?y") == "http://a/b/c/d;p?y");
    BOOST_TEST(resolve_reference(baseURI, "g?y") == "http://a/b/c/g?y");
    BOOST_TEST(resolve_reference(baseURI, "#s") == "http://a/b/c/d;p?q#s");
    BOOST_TEST(resolve_reference(baseURI, "g#s") == "http://a/b/c/g#s");
    BOOST_TEST(resolve_reference(baseURI, "g?y#s") == "http://a/b/c/g?y#s");
    BOOST_TEST(resolve_reference(baseURI, ";x") == "http://a/b/c/;x");
    BOOST_TEST(resolve_reference(baseURI, "g;x") == "http://a/b/c/g;x");
    BOOST_TEST(resolve_reference(baseURI, "g;x?y#s") == "http://a/b/c/g;x?y#s");
    BOOST_TEST(resolve_reference(baseURI, "") == "http://a/b/c/d;p?q");
    BOOST_TEST(resolve_reference(baseURI, ".") == "http://a/b/c/");
    BOOST_TEST(resolve_reference(baseURI, "./") == "http://a/b/c/");
    BOOST_TEST(resolve_reference(baseURI, "..") == "http://a/b/");
    BOOST_TEST(resolve_reference(baseURI, "../") == "http://a/b/");
    BOOST_TEST(resolve_reference(baseURI, "../g") == "http://a/b/g");
    BOOST_TEST(resolve_reference(baseURI, "../..") == "http://a/");
    BOOST_TEST(resolve_reference(baseURI, "../../") == "http://a/");
    BOOST_TEST(resolve_reference(baseURI, "../../g") == "http://a/g");

    BOOST_TEST(resolve_reference(baseURI, "../../../g") == "http://a/g");
    BOOST_TEST(resolve_reference(baseURI, "../../../../g") == "http://a/g");

    BOOST_TEST(resolve_reference(baseURI, "/./g") == "http://a/g");
    BOOST_TEST(resolve_reference(baseURI, "/../g") == "http://a/g");
    BOOST_TEST(resolve_reference(baseURI, "g.") == "http://a/b/c/g.");
    BOOST_TEST(resolve_reference(baseURI, ".g") == "http://a/b/c/.g");
    BOOST_TEST(resolve_reference(baseURI, "g..") == "http://a/b/c/g..");
    BOOST_TEST(resolve_reference(baseURI, "..g") == "http://a/b/c/..g");

    BOOST_TEST(resolve_reference(baseURI, "g?y/./x") == "http://a/b/c/g?y/./x");
    BOOST_TEST(resolve_reference(baseURI, "g?y/../x") == "http://a/b/c/g?y/../x");
    BOOST_TEST(resolve_reference(baseURI, "g#s/./x") == "http://a/b/c/g#s/./x");
    BOOST_TEST(resolve_reference(baseURI, "g#s/../x") == "http://a/b/c/g#s/../x");

    BOOST_TEST(resolve_reference(baseURI, "http:g") == "http:g");
}


BOOST_AUTO_TEST_CASE(percent_encoding)
{
    BOOST_TEST(percent_encode(" foo*/123;bar%") == "%20foo%2A%2F123%3Bbar%25");
    BOOST_TEST(percent_encode(" foo*/123;bar%", "/;") == "%20foo%2A/123;bar%25");

    BOOST_TEST(percent_decode("%20foo%2A%2F123%3Bbar%25") == " foo*/123;bar%");
    BOOST_TEST(percent_decode("%20foo%2a%2f123%3bbar%25") == " foo*/123;bar%");
    BOOST_CHECK_THROW(percent_decode("%G0"), std::domain_error);
    BOOST_CHECK_THROW(percent_decode("%0G"), std::domain_error);

    BOOST_TEST(percent_encode_uri("http", "user name@10.0.0.1:passwd", "/foo/bar baz", "q1=foo bar&q2=baz;q3", "foo bar") == "http://user%20name@10.0.0.1:passwd/foo/bar%20baz?q1=foo%20bar&q2=baz;q3#foo%20bar");
}


BOOST_AUTO_TEST_CASE(file_uri_conversions)
{
    // From path to URI
    BOOST_TEST(path_to_file_uri("/foo bar/baz") == "file:///foo%20bar/baz");
    BOOST_TEST(path_to_file_uri(cosim::filesystem::path()) == "file:");
#ifdef _WIN32
    BOOST_TEST(path_to_file_uri("\\foo bar\\baz") == "file:///foo%20bar/baz");
    BOOST_TEST(path_to_file_uri("/foo bar/baz") == "file:///foo%20bar/baz");
    BOOST_TEST(path_to_file_uri("c:\\foo bar\\baz") == "file:///c:/foo%20bar/baz");
#endif

    // From URI to path
#ifdef _WIN32
    BOOST_TEST(file_uri_to_path("file:///foo%20bar/baz") == "\\foo bar\\baz");
    BOOST_TEST(file_uri_to_path("file:///c:/foo%20bar/baz") == "c:\\foo bar\\baz");
    BOOST_TEST(file_uri_to_path("file://localhost/foo%20bar/baz") == "\\foo bar\\baz");
    BOOST_TEST(file_uri_to_path("file://localhost/c:/foo%20bar/baz") == "c:\\foo bar\\baz");
#else
    BOOST_TEST(file_uri_to_path("file:///foo%20bar/baz") == "/foo bar/baz");
    BOOST_TEST(file_uri_to_path("file:///c:/foo%20bar/baz") == "/c:/foo bar/baz");
    BOOST_TEST(file_uri_to_path("file://localhost/foo%20bar/baz") == "/foo bar/baz");
    BOOST_TEST(file_uri_to_path("file://localhost/c:/foo%20bar/baz") == "/c:/foo bar/baz");
#endif
    BOOST_CHECK_THROW(file_uri_to_path("http://foo/bar"), std::invalid_argument);
    BOOST_CHECK_THROW(file_uri_to_path("file://foo/bar"), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(file_query_uri_conversions)
{
    // From URI file query to path
    const auto baseURI = uri("file:///c:/foo/bar");
#ifdef _WIN32
    BOOST_TEST(file_query_uri_to_path(baseURI, "proxyfmu://foo%20bar/bar?file=baz.txt") == "c:\\foo\\baz.txt");
    BOOST_TEST(file_query_uri_to_path(baseURI, "proxyfmu://foo%20bar/bar/foo?file=bar/baz.txt") == "c:\\foo\\bar\\baz.txt");
    BOOST_TEST(file_query_uri_to_path(baseURI, "http://foo%20bar/bar?file=baz.txt") == "c:\\foo\\baz.txt");
    BOOST_TEST(file_query_uri_to_path(baseURI, "http://foo%20bar/foo/bar?file=bar%20foo/baz.txt") == "c:\\foo\\bar foo\\baz.txt");
    BOOST_TEST(file_query_uri_to_path(baseURI, "proxyfmu://foo/bar?file=file:///c:/baz.txt") == "c:\\baz.txt");
    BOOST_TEST(file_query_uri_to_path(baseURI, "http://foo%20baz/bar?file=file:///c:/foo/bar/baz.txt") == "c:\\foo\\bar\\baz.txt");
    BOOST_TEST(file_query_uri_to_path(baseURI, "http://foo%20baz/bar?foo=baz.txt") == "c:\\foo");
#else
    BOOST_TEST(file_query_uri_to_path(baseURI, "proxyfmu://foo%20bar/bar?file=baz.txt") == "/c:/foo/baz.txt");
    BOOST_TEST(file_query_uri_to_path(baseURI, "proxyfmu://foo%20bar/bar/foo?file=bar/baz.txt") == "/c:/foo/bar/baz.txt");
    BOOST_TEST(file_query_uri_to_path(baseURI, "http://foo%20bar/bar?file=baz.txt") == "/c:/foo/baz.txt");
    BOOST_TEST(file_query_uri_to_path(baseURI, "http://foo%20bar/foo/bar?file=bar%20foo/baz.txt") == "/c:/foo/bar foo/baz.txt");
    BOOST_TEST(file_query_uri_to_path(baseURI, "proxyfmu://foo/bar?file=file:///c:/baz.txt") == "/c:/baz.txt");
    BOOST_TEST(file_query_uri_to_path(baseURI, "http://foo%20baz/bar?file=file:///c:/foo/bar/baz.txt") == "/c:/foo/bar/baz.txt");
    BOOST_TEST(file_query_uri_to_path(baseURI, "http://foo%20baz/bar?foo=baz.txt") == "/c:/foo");
#endif
    BOOST_CHECK_THROW(file_query_uri_to_path(baseURI, "foo/bar/baz.txt"), std::invalid_argument);
}
