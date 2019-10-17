#include <cse/algorithm.hpp>
#include <cse/execution.hpp>
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
        boost::filesystem::path xmlPath = boost::filesystem::path(testDataDir) / "ssp" / "demo";

        auto resolver = cse::default_model_uri_resolver();
        auto [system, params, simInfo] = cse::load_ssp_v2(*resolver, xmlPath);

        REQUIRE(*simInfo.algorithmDescription == "FixedStepAlgorithm");
        REQUIRE(*simInfo.stepSize == cse::to_duration(1e-4));
        REQUIRE(*simInfo.startTime == cse::time_point());
        REQUIRE(*simInfo.stopTime == cse::to_time_point(2e-4));

        auto execution = cse::execution(
            *simInfo.startTime,
            std::make_shared<cse::fixed_step_algorithm>(*simInfo.stepSize));
        auto simulatorMap = cse::inject_system_structure(execution, system, params);

        REQUIRE(simulatorMap.size() == 2);
        REQUIRE(simulatorMap.count("CraneController"));
        REQUIRE(simulatorMap.count("KnuckleBoomCrane"));

        auto obs = std::make_shared<cse::last_value_observer>();
        execution.add_observer(obs);
        auto result = execution.simulate_until(cse::to_time_point(1e-3));
        REQUIRE(result.get());

        cse::simulator_index i = simulatorMap.at("KnuckleBoomCrane");
        double realValue = -1.0;
        cse::value_reference reference =
            system.get_variable_description({"KnuckleBoomCrane", "Spring_Joint.k"}).reference;
        obs->get_real(i, gsl::make_span(&reference, 1), gsl::make_span(&realValue, 1));

        double magicNumberFromSsdFile = 0.005;
        REQUIRE(std::fabs(realValue - magicNumberFromSsdFile) < 1e-9);

        cse::value_reference reference2 =
            system.get_variable_description({"KnuckleBoomCrane", "mt0_init"}).reference;
        obs->get_real(i, gsl::make_span(&reference2, 1), gsl::make_span(&realValue, 1));

        magicNumberFromSsdFile = 69.0;
        REQUIRE(std::fabs(realValue - magicNumberFromSsdFile) < 1e-9);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what();
        return 1;
    }
    return 0;
}
