#define BOOST_TEST_MODULE orchestration.hpp unittests
#include <cosim/fs_portability.hpp>
#include <cosim/algorithm/fixed_step_algorithm.hpp>
#include <cosim/execution.hpp>
#include <cosim/fmi/fmu.hpp>
#include <cosim/fmi/v3/fmu.hpp>
#include <cosim/orchestration.hpp>
#include <cosim/system_structure.hpp>
#include <cosim/uri.hpp>

#include <boost/test/unit_test.hpp>

#include <stdexcept>
#include <utility>


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
    const auto fmuModel = std::dynamic_pointer_cast<cosim::fmu_backed_model>(model);
    BOOST_REQUIRE(fmuModel);
    BOOST_CHECK(fmuModel->fmu());
    BOOST_CHECK_THROW(cosim::fmu_backed_model(nullptr), std::invalid_argument);
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

BOOST_AUTO_TEST_CASE(fmi3_custom_instantiation_hook_test)
{
    const auto directory = std::getenv("REFERENCE_FMU_DIR");
    BOOST_REQUIRE(directory);
    auto resolver = cosim::default_model_uri_resolver();
    auto model = resolver->lookup_model(cosim::path_to_file_uri(
        cosim::filesystem::path(directory) / "IntermediateFeedthrough.fmu"));
    cosim::system_structure system;
    system.add_entity("source", model);
    system.add_entity("target", model);
    auto algorithm = std::make_shared<cosim::fixed_step_algorithm>(cosim::to_duration(0.1), 1);
    cosim::execution run(cosim::to_time_point(0), algorithm);
    int calls = 0;
    cosim::model_instantiator instantiate =
        [&calls](const std::shared_ptr<cosim::model>& selected, std::string_view name)
            -> std::shared_ptr<cosim::slave> {
        if (name != "source") return selected->instantiate(name);
        auto backed = std::dynamic_pointer_cast<cosim::fmu_backed_model>(selected);
        auto fmu = backed ? std::dynamic_pointer_cast<cosim::fmi::v3::fmu>(backed->fmu()) : nullptr;
        if (!fmu) throw std::invalid_argument("source must be an FMI 3 FMU");
        cosim::fmi::v3::co_simulation_callbacks options;
        options.intermediate_update =
            [&calls](cosim::fmi::v3::slave_instance&, cosim::fmi::v3::intermediate_update_info&) {
                ++calls;
            };
        return fmu->instantiate_v3_slave(name, std::move(options));
    };
    const auto ids = cosim::inject_system_structure(run, system, {}, instantiate);
    BOOST_TEST(ids.simulators.size() == 2U);
    run.initialize();
    BOOST_CHECK(run.simulate_until(cosim::to_time_point(0.1)));
    BOOST_CHECK_GT(calls, 0);
}
