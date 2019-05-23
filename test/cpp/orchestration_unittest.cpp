#define BOOST_TEST_MODULE orchestration.hpp unittests
#include <cse/orchestration.hpp>
#include <cse/uri.hpp>

#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>


BOOST_AUTO_TEST_CASE(file_uri_sub_resolver_absolute_path_test)
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_TEST_REQUIRE(!!testDataDir);
    const auto path = boost::filesystem::path(testDataDir) / "fmi2" / "Clock.fmu";
    const auto uri = cse::path_to_file_uri(path);

    cse::model_uri_resolver resolver;
    resolver.add_sub_resolver(std::make_shared<cse::file_uri_sub_resolver>());

    auto model = resolver.lookup_model(uri);
    BOOST_TEST(model->description()->name == "Clock");
}


BOOST_AUTO_TEST_CASE(file_uri_sub_resolver_relative_path_test)
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_TEST_REQUIRE(!!testDataDir);
    const auto basePath = boost::filesystem::path(testDataDir) / "some_base";
    const auto baseUri = cse::path_to_file_uri(basePath);

    cse::model_uri_resolver resolver;
    resolver.add_sub_resolver(std::make_shared<cse::file_uri_sub_resolver>());

    auto model = resolver.lookup_model(baseUri, "fmi2/Clock.fmu");
    BOOST_TEST(model->description()->name == "Clock");
}
