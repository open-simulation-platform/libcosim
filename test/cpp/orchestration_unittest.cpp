#define BOOST_TEST_MODULE orchestration.hpp unittests
#include <cse/orchestration.hpp>

#include <boost/test/unit_test.hpp>


BOOST_AUTO_TEST_CASE(file_uri_sub_resolver_absolute_path_test)
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_TEST_REQUIRE(!!testDataDir);

    std::string uri = "file://";

#ifdef _WIN32
    uri += "/";
#endif
    uri += std::string(testDataDir) + "/fmi2/Clock.fmu";

    cse::model_uri_resolver resolver;
    resolver.add_sub_resolver(std::make_shared<cse::file_uri_sub_resolver>());

    auto model = resolver.lookup_model("", uri);

    BOOST_TEST(model->description()->name == "Clock");
}

BOOST_AUTO_TEST_CASE(file_uri_sub_resolver_relative_path_test)
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_TEST_REQUIRE(!!testDataDir);

    std::string baseUri = "file://";

#ifdef _WIN32
    baseUri += "/";
#endif
    baseUri += std::string(testDataDir);
    std::string uri = "/fmi2/Clock.fmu";

    cse::model_uri_resolver resolver;
    resolver.add_sub_resolver(std::make_shared<cse::file_uri_sub_resolver>());

    auto model = resolver.lookup_model(baseUri, uri);

    BOOST_TEST(model->description()->name == "Clock");
}
