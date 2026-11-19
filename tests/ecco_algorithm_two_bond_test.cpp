/**
 * Regression test for: add_power_bonds() spuriously throws
 * "The number of powerbonds (1) is not equal to the number of unique power bond
 *  names (2)" when two bonds are configured.
 *
 * Root cause: the uniqueCount != numPowerBonds validation was placed inside the
 * per-bond loop in add_power_bonds() (osp_config_parser.cpp). After the first
 * bond is added, numPowerBonds == 1 but uniqueCount == 2 (computed up front from
 * all connections), causing a spurious throw. The check must run after all bonds
 * have been registered, i.e. outside the loop.
 *
 * This test uses two independent chassis+wheel pairs (OspSystemStructure_MultiBond.xml),
 * each connected by a separate power bond. Before the fix, load_osp_config() throws
 * on that file. After the fix it must load, simulate, and complete without error.
 */

#include <cosim/algorithm.hpp>
#include <cosim/fs_portability.hpp>
#include <cosim/log/simple.hpp>
#include <cosim/osp_config_parser.hpp>
#include <cosim/time.hpp>

#include <cstdlib>
#include <exception>
#include <memory>
#include <stdexcept>


#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

int main()
{
    try {
        const auto testDataDir = std::getenv("TEST_DATA_DIR");
        REQUIRE(testDataDir);
        cosim::log::setup_simple_console_logging();
        cosim::log::set_global_output_level(cosim::log::info);

        constexpr cosim::time_point endTime = cosim::to_time_point(0.1);

        auto resolver = cosim::default_model_uri_resolver();
        const auto configPath = cosim::filesystem::path(testDataDir) / "fmi2" / "quarter_truck" / "OspSystemStructure_MultiBond.xml";

        // Before the fix this throws:
        //   "The number of powerbonds (1) is not equal to the number of unique
        //    power bond names (2) ..."
        // because the uniqueCount check fired inside the per-bond loop after
        // only the first of the two bonds had been added.
        const auto config = cosim::load_osp_config(configPath, *resolver);

        auto ecco_params = std::get<cosim::ecco_algorithm_params>(config.algorithm_configuration);
        auto ecco_algo = std::make_shared<cosim::ecco_algorithm>(ecco_params);

        auto execution = cosim::execution(config.start_time, ecco_algo);

        const auto entityMaps = cosim::inject_system_structure(
            execution, config.system_structure, config.initial_values);

        REQUIRE(entityMaps.simulators.size() == 4); // chassis1, wheel1, chassis2, wheel2

        auto simResult = execution.simulate_until(endTime);
        REQUIRE(simResult);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
