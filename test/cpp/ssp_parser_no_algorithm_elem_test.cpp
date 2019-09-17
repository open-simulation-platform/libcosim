#include <cse/algorithm.hpp>
#include <cse/log/simple.hpp>
#include <cse/observer/last_value_observer.hpp>
#include <cse/ssp_parser.hpp>

#include <boost/filesystem.hpp>

#include <cstdlib>
#include <exception>

#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

int main()
{
    try {
        cse::log::setup_simple_console_logging();
        cse::log::set_global_output_level(cse::log::info);

        const auto testDataDir = std::getenv("TEST_DATA_DIR");
        REQUIRE(testDataDir);
        boost::filesystem::path xmlPath = boost::filesystem::path(testDataDir) / "ssp" / "demo" / "no_algorithm_element";

        auto resolver = cse::default_model_uri_resolver();
        auto simulation = cse::load_ssp(
            *resolver,
            xmlPath,
            std::make_unique<cse::fixed_step_algorithm>(cse::to_duration(1e-4)));
        auto& execution = simulation.first;

        auto& simulator_map = simulation.second;
        REQUIRE(simulator_map.size() == 2);

        // ssp defaultExperiment startTime=5
        REQUIRE(cse::to_double_time_point(execution.current_time()) == 5.0);

        auto obs = std::make_shared<cse::last_value_observer>();
        execution.add_observer(obs);
        auto result = execution.simulate_until(cse::to_time_point(1e-3));
        REQUIRE(result.get());

        cse::simulator_index i = simulator_map.at("KnuckleBoomCrane").index;
        double realValue = -1.0;
        cse::value_reference reference = cse::find_variable(simulator_map.at("KnuckleBoomCrane").description, "Spring_Joint.k").reference;
        obs->get_real(i, gsl::make_span(&reference, 1), gsl::make_span(&realValue, 1));

        double magicNumberFromSsdFile = 0.005;
        REQUIRE(std::fabs(realValue - magicNumberFromSsdFile) < 1e-9);

        cse::value_reference reference2 = cse::find_variable(simulator_map.at("KnuckleBoomCrane").description, "mt0_init").reference;
        obs->get_real(i, gsl::make_span(&reference2, 1), gsl::make_span(&realValue, 1));

        magicNumberFromSsdFile = 69.0;
        REQUIRE(std::fabs(realValue - magicNumberFromSsdFile) < 1e-9);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what();
        return 1;
    }
    return 0;
}
