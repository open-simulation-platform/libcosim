#include <cosim/algorithm.hpp>
#include <cosim/fs_portability.hpp>
#include <cosim/log/simple.hpp>
#include <cosim/observer/file_observer.hpp>
#include <cosim/orchestration.hpp>
#include <cosim/osp_config_parser.hpp>

#include <cstdlib>
#include <exception>
#include <memory>
#include <stdexcept>

#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

std::ostream& operator<<(std::ostream& out, const cosim::system_structure::power_bond& pb)
{
    return out << "Powerbond is comprised of connections (source variable --> target variable):" << std::endl
               << pb.connection_a.source << " ---> " << pb.connection_a.target << std::endl
               << pb.connection_b.source << " ---> " << pb.connection_b.target << std::endl;
}

int main()
{
    try {
        const auto testDataDir = std::getenv("TEST_DATA_DIR");
        REQUIRE(testDataDir);
        cosim::log::setup_simple_console_logging();
        cosim::log::set_global_output_level(cosim::log::debug);
        
        auto resolver = cosim::default_model_uri_resolver();
        auto configPath = cosim::filesystem::path(testDataDir) / "fmi2" / "quarter_truck";
        const auto logXmlPath = cosim::filesystem::path(testDataDir) / "fmi2" / "quarter_truck" / "LogConfig.xml";
        const auto config = cosim::load_osp_config(configPath, *resolver);

        auto ecco_params = std::get<cosim::ecco_algorithm_params>(config.algorithm_configuration);
        auto ecco_algo = std::make_shared<cosim::ecco_algorithm>(ecco_params);

        auto execution = cosim::execution(config.start_time, ecco_algo);

        const auto entityMaps = cosim::inject_system_structure(execution, config.system_structure, config.initial_values);
        const auto realTimeConfig = execution.get_real_time_config();

        const auto pbs = config.system_structure.get_power_bonds();

        for (const auto& [pbName, pb] : pbs) {
            std::cout << "Power bond " << pbName << std::endl;
            std::cout << pb;
        }

        REQUIRE(entityMaps.simulators.size() == 2);
        REQUIRE(!realTimeConfig->real_time_simulation);

        const auto logPath = cosim::filesystem::current_path() / "logs";
        std::cout << "Log path:" << logPath.c_str();
        auto file_obs = std::make_unique<cosim::file_observer>(logPath, logXmlPath);
        execution.add_observer(std::move(file_obs));

        // constexpr cosim::time_point endTime = cosim::to_time_point(0.1);
        // auto simResult = execution.simulate_until(endTime);
        // REQUIRE(simResult);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what();
        return 1;
    }
    return 0;
}
