#include <cse/algorithm/fixed_step_algorithm.hpp>
#include <cse/cse_config_parser.hpp>
#include <cse/log/simple.hpp>
#include <cse/observer/last_value_observer.hpp>
#include <cse/orchestration.hpp>

#include <boost/filesystem.hpp>

#include <cstdlib>
#include <exception>


#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

void test(const boost::filesystem::path& configPath, int expectedNumConnections)
{
    auto resolver = cse::default_model_uri_resolver();
    const auto config = cse::load_cse_config(configPath, *resolver);
    auto execution = cse::execution(
        config.start_time,
        std::make_shared<cse::fixed_step_algorithm>(config.step_size));

    const auto entityMaps = cse::inject_system_structure(
        execution, config.system_structure, config.initial_values);
    REQUIRE(entityMaps.simulators.size() == 4);
    REQUIRE(boost::size(config.system_structure.connections()) == expectedNumConnections);

    auto obs = std::make_shared<cse::last_value_observer>();
    execution.add_observer(obs);

    auto result = execution.simulate_until(cse::to_time_point(1e-3));
    REQUIRE(result.get());

    const auto simIndex = entityMaps.simulators.at("CraneController");
    const auto varReference =
        config.system_structure.get_variable_description({"CraneController", "cl1_min"}).reference;
    double realValue = -1.0;
    obs->get_real(simIndex, gsl::make_span(&varReference, 1), gsl::make_span(&realValue, 1));

    double magicNumberFromConf = 2.2;
    REQUIRE(std::fabs(realValue - magicNumberFromConf) < 1e-9);
}

int main()
{
    try {
        cse::log::setup_simple_console_logging();
        cse::log::set_global_output_level(cse::log::info);

        const auto testDataDir = std::getenv("TEST_DATA_DIR");
        REQUIRE(testDataDir);
        test(boost::filesystem::path(testDataDir) / "msmi",7);
        test(boost::filesystem::path(testDataDir) / "msmi" / "OspSystemStructure_Bond.xml",9);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what();
        return 1;
    }
    return 0;
}
