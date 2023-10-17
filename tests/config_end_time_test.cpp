#include <cosim/algorithm/fixed_step_algorithm.hpp>
#include <cosim/fs_portability.hpp>
#include <cosim/log/simple.hpp>
#include <cosim/observer/last_value_observer.hpp>
#include <cosim/orchestration.hpp>
#include <cosim/osp_config_parser.hpp>

#include <cstdlib>
#include <exception>
#include <iostream>

#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)


void test(const cosim::filesystem::path& configPath)
{
    auto resolver = cosim::default_model_uri_resolver();
    const auto config = cosim::load_osp_config(configPath, *resolver);
    auto execution = cosim::execution(
        config.start_time,
        std::make_shared<cosim::fixed_step_algorithm>(config.step_size));

    const auto entityMaps = cosim::inject_system_structure(
        execution, config.system_structure, config.initial_values);

    REQUIRE(entityMaps.simulators.size() == 4);

    auto obs = std::make_shared<cosim::last_value_observer>();
    execution.add_observer(obs);

    if (config.end_time.has_value()) {
        REQUIRE(config.end_time.value().time_since_epoch().count() / 1e9 == 0.001);
        auto result = execution.simulate_until(config.end_time);
        REQUIRE(result);
    } else {
        auto result = execution.simulate_until(cosim::to_time_point(0.001));
        REQUIRE(result);
    }
}

int main()
{
    try {
        cosim::log::setup_simple_console_logging();
        cosim::log::set_global_output_level(cosim::log::info);

        const auto testDataDir = std::getenv("TEST_DATA_DIR");
        REQUIRE(testDataDir);

        test(cosim::filesystem::path(testDataDir) / "msmi" / "OspSystemStructure.xml");
        test(cosim::filesystem::path(testDataDir) / "msmi" / "OspSystemStructure_EndTime.xml");
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what();
        return 1;
    }
    return 0;
}