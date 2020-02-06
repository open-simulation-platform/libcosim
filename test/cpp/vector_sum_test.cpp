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

    REQUIRE(simulator_map.size() == 4);

    auto obs = std::make_shared<cse::last_value_observer>();
    execution.add_observer(obs);

    auto i1 = simulator_map.at("vector1").index;
    auto i2 = simulator_map.at("vector2").index;
    auto i3 = simulator_map.at("vector3").index;
    execution.set_real_initial_value(i1, 0, 1.0);
    execution.set_real_initial_value(i1, 1, 2.0);
    execution.set_real_initial_value(i1, 2, 3.0);
    execution.set_real_initial_value(i2, 0, 4.0);
    execution.set_real_initial_value(i2, 1, 5.0);
    execution.set_real_initial_value(i2, 2, 6.0);
    execution.set_real_initial_value(i3, 0, 7.0);
    execution.set_real_initial_value(i3, 1, 8.0);
    execution.set_real_initial_value(i3, 2, 9.0);

    execution.step();
    execution.step();

    cse::simulator_index lastOne = simulator_map.at("vector4").index;
    double realValues[3];
    cse::value_reference references[] = {3, 4, 5};
    obs->get_real(lastOne, gsl::make_span(references), gsl::make_span(realValues));

    double exptectedValues[]{12.0, 15.0, 18.0};
    for (int i = 0; i < 3; i++) {
        REQUIRE(realValues[i] == exptectedValues[i]);
    }
}

int main()
{
    try {
        cse::log::setup_simple_console_logging();
        cse::log::set_global_output_level(cse::log::info);

        const auto testDataDir = std::getenv("TEST_DATA_DIR");
        REQUIRE(testDataDir);
        test(boost::filesystem::path(testDataDir) / "msmi" / "OspSystemStructure_vectorSum.xml");
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what();
        return 1;
    }
    return 0;
}
