#define BOOST_TEST_MODULE uri.hpp unittests
#include <cse/uri.hpp>

#include <boost/test/unit_test.hpp>

using namespace cse;


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
    BOOST_TEST(httpURI2 == httpURI);

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
