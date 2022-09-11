
#include <cosim/fs_portability.hpp>
#include <cosim/orchestration.hpp>
#include <cosim/uri.hpp>

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>


TEST_CASE("file_uri_sub_resolver_absolute_path_test")
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    REQUIRE(!!testDataDir);
    const auto path = cosim::filesystem::path(testDataDir) / "fmi2" / "Clock.fmu";
    const auto uri = cosim::path_to_file_uri(path);

    cosim::model_uri_resolver resolver;
    resolver.add_sub_resolver(std::make_shared<cosim::fmu_file_uri_sub_resolver>());

    auto model = resolver.lookup_model(uri);
    CHECK(model->description()->name == "Clock");
}


TEST_CASE("file_uri_sub_resolver_relative_path_test")
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    REQUIRE(!!testDataDir);
    const auto basePath = cosim::filesystem::path(testDataDir) / "some_base";
    const auto baseUri = cosim::path_to_file_uri(basePath);

    cosim::model_uri_resolver resolver;
    resolver.add_sub_resolver(std::make_shared<cosim::fmu_file_uri_sub_resolver>());

    auto model = resolver.lookup_model(baseUri, "fmi2/Clock.fmu");
    CHECK(model->description()->name == "Clock");
}
