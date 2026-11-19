#include <cosim/algorithm/fixed_step_algorithm.hpp>
#include <cosim/exception.hpp>
#include <cosim/fs_portability.hpp>
#include <cosim/log/simple.hpp>
#include <cosim/observer/last_value_observer.hpp>
#include <cosim/orchestration.hpp>
#include <cosim/osp_config_parser.hpp>

#include <cstdlib>
#include <exception>
#include <fstream>


#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

void test(const cosim::filesystem::path& configPath, size_t expectedNumConnections)
{
    auto resolver = cosim::default_model_uri_resolver();
    const auto config = cosim::load_osp_config(configPath, *resolver);
    auto algorithm_cfg = std::get<cosim::fixed_step_algorithm_params>(config.algorithm_configuration);

    auto execution = cosim::execution(
        config.start_time,
        std::make_shared<cosim::fixed_step_algorithm>(algorithm_cfg));

    const auto entityMaps = cosim::inject_system_structure(
        execution, config.system_structure, config.initial_values);

    REQUIRE(entityMaps.simulators.size() == 4);
    REQUIRE(boost::size(config.system_structure.connections()) == expectedNumConnections);

    auto obs = std::make_shared<cosim::last_value_observer>();
    execution.add_observer(obs);

    auto result = execution.simulate_until(cosim::to_time_point(0.01));
    REQUIRE(result);

    const auto simIndex = entityMaps.simulators.at("CraneController");
    const auto varReference1 = config.system_structure.get_variable_description({"CraneController", "cl1_min"}).reference;
    double realValue = -1.0;

    obs->get_real(simIndex, gsl::make_span(&varReference1, 1), gsl::make_span(&realValue, 1));

    double magicNumberFromConf = 2.2;
    REQUIRE(std::fabs(realValue - magicNumberFromConf) < 1e-9);
}

// Invalid or missing configuration files must be reported as cosim::error
// with errc::bad_file, so that callers (and language bindings) get an
// error code rather than a plain std::runtime_error.
void require_bad_file(const cosim::filesystem::path& configPath)
{
    auto resolver = cosim::default_model_uri_resolver();
    try {
        cosim::load_osp_config(configPath, *resolver);
    } catch (const cosim::error& e) {
        REQUIRE(e.code() == cosim::errc::bad_file);
        return;
    }
    throw std::runtime_error("Expected cosim::error for " + configPath.string());
}

void test_invalid_configs()
{
    const auto dir = cosim::filesystem::temp_directory_path() / "libcosim_osp_config_parser_test";
    cosim::filesystem::create_directories(dir);

    const auto invalid = dir / "OspSystemStructure.xml";
    {
        std::ofstream f(invalid);
        f << "<?xml version=\"1.0\"?>\n"
          << "<OspSystemStructure xmlns=\"http://opensimulationplatform.com/MSMI/OSPSystemStructure\">"
          << "<NotAValidElement/></OspSystemStructure>\n";
    }
    require_bad_file(invalid);
    require_bad_file(dir / "does_not_exist.xml");

    cosim::filesystem::remove_all(dir);
}

int main()
{
    try {
        cosim::log::setup_simple_console_logging();
        cosim::log::set_global_output_level(cosim::log::info);

        const auto testDataDir = std::getenv("TEST_DATA_DIR");
        REQUIRE(testDataDir);
        test(cosim::filesystem::path(testDataDir) / "msmi", 7);
        test(cosim::filesystem::path(testDataDir) / "msmi" / "OspSystemStructure_Bond.xml", 9);
        test_invalid_configs();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what();
        return 1;
    }
    return 0;
}
