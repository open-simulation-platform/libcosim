#define BOOST_TEST_MODULE orchestration.hpp unittests
#include <cosim/fs_portability.hpp>
#include <cosim/orchestration.hpp>
#include <cosim/uri.hpp>

#include <boost/test/unit_test.hpp>


BOOST_AUTO_TEST_CASE(file_uri_sub_resolver_absolute_path_test)
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_TEST_REQUIRE(!!testDataDir);
    const auto path = cosim::filesystem::path(testDataDir) / "fmi2" / "vector.fmu";
    const auto uri = cosim::path_to_file_uri(path);

    cosim::model_uri_resolver resolver;
    resolver.add_sub_resolver(std::make_shared<cosim::fmu_file_uri_sub_resolver>());

    auto model = resolver.lookup_model(uri);
    BOOST_TEST(model->description()->name == "com.open-simulation-platform.vector");
}


BOOST_AUTO_TEST_CASE(file_uri_sub_resolver_relative_path_test)
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_TEST_REQUIRE(!!testDataDir);
    const auto basePath = cosim::filesystem::path(testDataDir) / "some_base";
    const auto baseUri = cosim::path_to_file_uri(basePath);

    cosim::model_uri_resolver resolver;
    resolver.add_sub_resolver(std::make_shared<cosim::fmu_file_uri_sub_resolver>());

    auto model = resolver.lookup_model(baseUri, "fmi2/vector.fmu");
    BOOST_TEST(model->description()->name == "com.open-simulation-platform.vector");
}
