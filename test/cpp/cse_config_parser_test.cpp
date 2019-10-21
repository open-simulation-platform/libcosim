#include <cse/connection/sum_connection.hpp>
#include <cse/cse_config_parser.hpp>
#include <cse/log/simple.hpp>
#include <cse/observer/last_value_observer.hpp>
#include <cse/orchestration.hpp>

#include <boost/filesystem.hpp>

#include <cstdlib>
#include <exception>


#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

void test(const boost::filesystem::path& configPath)
{
    auto resolver = cse::default_model_uri_resolver();
    auto simulation = cse::load_cse_config(*resolver, configPath, cse::to_time_point(0.0));
    auto& execution = simulation.first;
    auto& simulator_map = simulation.second;

    REQUIRE(simulator_map.size() == 2);
    REQUIRE(simulator_map.at("CraneController").source == "../ssp/demo/CraneController.fmu");
    REQUIRE(simulator_map.at("KnuckleBoomCrane").source == "../ssp/demo/KnuckleBoomCrane.fmu");

    for (const auto& connection : execution.get_connections()) {
        if (const auto sum = std::dynamic_pointer_cast<cse::sum_connection>(connection)) {
            REQUIRE(sum->get_sources().length() == 3);
        }
    }

    auto obs = std::make_shared<cse::last_value_observer>();
    execution.add_observer(obs);

    auto result = execution.simulate_until(cse::to_time_point(1e-3));
    REQUIRE(result.get());

    cse::simulator_index i = simulator_map.at("CraneController").index;
    double realValue = -1.0;
    cse::value_reference reference = cse::find_variable(simulator_map.at("CraneController").description, "cl1_min").reference;
    obs->get_real(i, gsl::make_span(&reference, 1), gsl::make_span(&realValue, 1));

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
        test(boost::filesystem::path(testDataDir) / "msmi");
        test(boost::filesystem::path(testDataDir) / "msmi" / "OspSystemStructure_Bond.xml");
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what();
        return 1;
    }
    return 0;
}
