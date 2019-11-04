#include "mock_slave.hpp"

#include <cse/algorithm.hpp>
#include <cse/async_slave.hpp>
#include <cse/execution.hpp>
#include <cse/log/simple.hpp>
#include <cse/manipulator/scenario_manager.hpp>
#include <cse/observer/time_series_observer.hpp>

#include <exception>
#include <memory>
#include <stdexcept>
#include <vector>

#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

int main()
{
    try {
        cse::log::setup_simple_console_logging();
        cse::log::set_global_output_level(cse::log::trace);

        constexpr cse::time_point startTime = cse::to_time_point(0.0);
        constexpr cse::time_point endTime = cse::to_time_point(1.5);
        constexpr cse::duration stepSize = cse::to_duration(0.1);

        auto execution = cse::execution(startTime, std::make_unique<cse::fixed_step_algorithm>(stepSize));

        auto observer = std::make_shared<cse::time_series_observer>();
        execution.add_observer(observer);
        auto scenarioManager = std::make_shared<cse::scenario_manager>();
        execution.add_manipulator(scenarioManager);

        auto simIndex = execution.add_slave(
            cse::make_pseudo_async(std::make_unique<mock_slave>(
                [](double x) { return x + 1.234; },
                [](int y) { return y + 2; })),
            "slave uno");

        observer->start_observing(cse::variable_id{simIndex, cse::variable_type::real, 0});
        observer->start_observing(cse::variable_id{simIndex, cse::variable_type::integer, 0});

        const auto testDataDir = std::getenv("TEST_DATA_DIR");
        REQUIRE(testDataDir);
        boost::filesystem::path jsonPath = boost::filesystem::path(testDataDir) / "scenarios" / "scenario2.json";
        scenarioManager->load_scenario(jsonPath, startTime);

        constexpr bool input = true;
        constexpr bool output = false;

        auto simResult = execution.simulate_until(endTime);
        REQUIRE(simResult.get());

        const int numSamples = 15;
        double realOutputValues[numSamples];
        //int intOutputValues[numSamples];
        cse::step_number steps[numSamples];
        cse::time_point times[numSamples];

        size_t realSamplesRead = observer->get_real_samples(simIndex, 0, 1, gsl::make_span(realOutputValues, numSamples), gsl::make_span(steps, numSamples), gsl::make_span(times, numSamples));
        //size_t intSamplesRead = observer->get_integer_samples(simIndex, 0, 1, gsl::make_span(intOutputValues, numSamples), gsl::make_span(steps, numSamples), gsl::make_span(times, numSamples));
        REQUIRE(realSamplesRead == numSamples);
        //REQUIRE(intSamplesRead == numSamples);

        //double expectedRealOutputs[] = {1.234, 1.234, 1.234, 1.234, 1.234, 0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9};
        //double expectedIntOutputs[] = {2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 2};

        for (size_t i = 0; i < realSamplesRead; i++) {
            std::cout << realOutputValues[i] << ", ";
            //REQUIRE(std::fabs(realOutputValues[i] - expectedRealOutputs[i]) < 1.0e-9);
            //REQUIRE(std::fabs(intOutputValues[i] - expectedIntOutputs[i]) < 1.0e-9);
        }
        std::cout << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
