#include <cosim/algorithm.hpp>
#include <cosim/fs_portability.hpp>
#include <cosim/log/simple.hpp>
#include <cosim/observer/file_observer.hpp>
#include <cosim/observer/last_value_observer.hpp>
#include <cosim/observer/time_series_observer.hpp>
#include <cosim/orchestration.hpp>
#include <cosim/osp_config_parser.hpp>
#include <cosim/time.hpp>

#include <cstdlib>
#include <exception>
#include <memory>
#include <stdexcept>

/**
 * This test showcases how the ECCO algorithm may be configured via the OSPSystemStructure XMLs.
 *
 * The OspSystemStructure in the quarter_truck directory can be used as an example configuration. The key points are:
 *
 * * <Algorithm> in OspSystemStructure now accepts either "ecco" or "fixedStep".
 * * The configuration for the Ecco algorithm can be added to the root of OspSystemStructure as seen in the quarter_truck example.
 * * To describe a powerbond, add the attribute powerbond="mypowerbond" to either a VariableConnection or VariableGroupConnection element.
 *   This defines a name for the powerbond that the parsing uses to group correctly.
 * * To define the individual variables, the attribute port has been added to the Variable element. Here the user must specify whether the
 *   variable is the input or output port of it's side of the bond. So, if we are coupling for instance a force <-> velocity bond, this
 *   results in a tuple with one input and one output port for each VariableConnection that is used in the bond.
 * * This information is then parsed by osp_config_parser, which results in a power_bond_map available through the system_structure object.
 * * Finally, the power_bond_map is iterated and power bonds added to the algorithm by execution::inject_system_structure.
 *
 **/

#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

int main()
{
    try {
        const auto testDataDir = std::getenv("TEST_DATA_DIR");
        REQUIRE(testDataDir);
        cosim::log::setup_simple_console_logging();
        cosim::log::set_global_output_level(cosim::log::debug);

        constexpr cosim::time_point endTime = cosim::to_time_point(0.1);

        auto resolver = cosim::default_model_uri_resolver();
        auto configPath = cosim::filesystem::path(testDataDir) / "fmi2" / "quarter_truck";
        const auto logXmlPath = cosim::filesystem::path(testDataDir) / "fmi2" / "quarter_truck" / "LogConfig.xml";
        const auto config = cosim::load_osp_config(configPath, *resolver);

        auto ecco_params = std::get<cosim::ecco_algorithm_params>(config.algorithm_configuration);
        auto ecco_algo = std::make_shared<cosim::ecco_algorithm>(ecco_params);

        auto execution = cosim::execution(config.start_time, ecco_algo);

        const auto entityMaps = cosim::inject_system_structure(execution, config.system_structure, config.initial_values);
        const auto realTimeConfig = execution.get_real_time_config();

        REQUIRE(entityMaps.simulators.size() == 2);
        REQUIRE(!realTimeConfig->real_time_simulation);

        const auto logPath = cosim::filesystem::current_path() / "logs";
        std::cout << "Log path:" << logPath.c_str();
        auto file_obs = std::make_unique<cosim::file_observer>(logPath, logXmlPath);
        execution.add_observer(std::move(file_obs));

        auto simResult = execution.simulate_until(endTime);
        REQUIRE(simResult);


    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
