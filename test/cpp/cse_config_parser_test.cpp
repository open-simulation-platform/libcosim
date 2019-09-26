#include <cse/log/simple.hpp>
#include <cse/orchestration.hpp>
#include <cse/cse_config_parser.hpp>

#include <boost/filesystem.hpp>

#include <cstdlib>
#include <exception>


#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

void test(const boost::filesystem::path& configPath)
{
    auto resolver = cse::default_model_uri_resolver();
    auto simulation = cse::load_cse_config(*resolver, configPath, "C:\\MC\\git\\cse-core\\test\\data\\msmi\\OspSystemStructure.xsd", cse::to_time_point(0.0));
    auto& execution = simulation.first;

    auto& simulator_map = simulation.second;
    REQUIRE(simulator_map.size() == 2);
    REQUIRE(simulator_map.at("CraneController").source == "../ssp/demo/CraneController.fmu");
    REQUIRE(simulator_map.at("KnuckleBoomCrane").source == "../ssp/demo/KnuckleBoomCrane.fmu");

    auto result = execution.simulate_until(cse::to_time_point(1e-3));
    REQUIRE(result.get());
}

int main()
{
    try {
        cse::log::setup_simple_console_logging();
        cse::log::set_global_output_level(cse::log::info);

        const auto testDataDir = std::getenv("TEST_DATA_DIR");
        REQUIRE(testDataDir);
        test(boost::filesystem::path(testDataDir) / "msmi");
        test(boost::filesystem::path(testDataDir) / "msmi" / "OspSystemStructure_Bond.xml");
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what();
        return 1;
    }
    return 0;
}
