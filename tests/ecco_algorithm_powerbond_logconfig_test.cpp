/**
 * Regression test for: crash "Variable with reference N not found in exposed variables"
 * when using the ECCO algorithm with a file_observer config that omits power bond input
 * variables (force/in_vel) from the logged variable list.
 *
 * Root cause: ecco_algorithm::impl::add_power_bond stored all four bond variable IDs but
 * never called expose_for_getting on them. adjust_step_size then called get_real() on all
 * four variables, which throws for any variable not previously exposed for getting. The
 * default file_observer (no config) accidentally hid the bug by exposing ALL variables;
 * a config-restricted observer only exposes the listed variables, triggering the crash for
 * FMU-input variables (force, in_vel) not present in the log config.
 */

#include <cosim/algorithm.hpp>
#include <cosim/fs_portability.hpp>
#include <cosim/log/simple.hpp>
#include <cosim/observer/file_observer.hpp>
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
        const auto configPath = cosim::filesystem::path(testDataDir) / "fmi2" / "quarter_truck" / "OspSystemStructure.xml";
        const auto config = cosim::load_osp_config(configPath, *resolver);

        auto ecco_params = std::get<cosim::ecco_algorithm_params>(config.algorithm_configuration);
        auto ecco_algo = std::make_shared<cosim::ecco_algorithm>(ecco_params);

        auto execution = cosim::execution(config.start_time, ecco_algo);

        // inject_system_structure registers the power bond (chassis <-> wheel) by calling
        // ecco_algorithm::add_power_bond internally.
        const auto entityMaps = cosim::inject_system_structure(execution, config.system_structure, config.initial_values);
        REQUIRE(entityMaps.simulators.size() == 2);

        // Build a log config that intentionally omits the FMU-input bond variables:
        //   chassis.force  (ref=4,  FMU input / bond "output_a")
        //   wheel.in_vel   (ref=7,  FMU input / bond "output_b")
        // Before the fix, adjust_step_size would call get_real() on these refs and throw
        // "Variable with reference N not found in exposed variables" because no observer
        // had called expose_for_getting on them.
        cosim::file_observer_config log_config{};
        log_config.set_timestamped_filenames(false);
        // Log only output variables for chassis — force (ref=4) is deliberately absent.
        log_config.log_simulator_variables("chassis", {"position", "velocity"});
        // Log only output variables for wheel — in_vel (ref=7) is deliberately absent.
        log_config.log_simulator_variables("wheel", {"position", "out_spring_damper_f", "velocity"});

        auto file_obs = std::make_unique<cosim::file_observer>(
            cosim::filesystem::current_path() / "powerbond_logconfig_logs",
            log_config);
        execution.add_observer(std::move(file_obs));

        // This must complete without throwing. Before the fix it would crash with:
        //   "Variable with reference N not found in exposed variables"
        auto simResult = execution.simulate_until(endTime);
        REQUIRE(simResult);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
