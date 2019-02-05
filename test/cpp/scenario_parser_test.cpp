#include "mock_slave.hpp"

#include <cse/scenario_parser.hpp>
#include <cse/log.hpp>

#include <exception>
#include <memory>
#include <stdexcept>
#include <vector>
#include <cse/algorithm.hpp>
#include <cse/observer/time_series_observer.hpp>
#include <cse/manipulator.hpp>

#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

int main()
{
    try {
        cse::log::set_global_output_level(cse::log::level::trace);

        constexpr cse::time_point startTime = cse::to_time_point(0.0);
        constexpr cse::time_point endTime = cse::to_time_point(1.0);
        constexpr cse::duration stepSize = cse::to_duration(0.1);

        cse::log::set_global_output_level(cse::log::level::trace);

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
        observer->start_observing(cse::variable_id{simIndex, cse::variable_type::real, 1});
        observer->start_observing(cse::variable_id{simIndex, cse::variable_type::integer, 0});
        observer->start_observing(cse::variable_id{simIndex, cse::variable_type::integer, 1});

        const auto testDataDir = std::getenv("TEST_DATA_DIR");
        REQUIRE(testDataDir);
        boost::filesystem::path jsonPath = boost::filesystem::path(testDataDir) / "scenarios" / "scenario1.json";
        scenarioManager->load_scenario(jsonPath, startTime);

        auto simResult = execution.simulate_until(endTime);
        REQUIRE(simResult.get());

        const int numSamples = 10;
        double realInputValues[numSamples];
        double realOutputValues[numSamples];
        int intInputValues[numSamples];
        int intOutputValues[numSamples];
        cse::step_number steps[numSamples];
        cse::time_point times[numSamples];

        size_t samplesRead = observer->get_real_samples(simIndex, 1, 1, gsl::make_span(realInputValues, numSamples), gsl::make_span(steps, numSamples), gsl::make_span(times, numSamples));
        REQUIRE(samplesRead == 10);
        samplesRead = observer->get_real_samples(simIndex, 0, 1, gsl::make_span(realOutputValues, numSamples), gsl::make_span(steps, numSamples), gsl::make_span(times, numSamples));
        REQUIRE(samplesRead == 10);
        samplesRead = observer->get_integer_samples(simIndex, 1, 1, gsl::make_span(intInputValues, numSamples), gsl::make_span(steps, numSamples), gsl::make_span(times, numSamples));
        REQUIRE(samplesRead == 10);
        samplesRead = observer->get_integer_samples(simIndex, 0, 1, gsl::make_span(intOutputValues, numSamples), gsl::make_span(steps, numSamples), gsl::make_span(times, numSamples));
        REQUIRE(samplesRead == 10);

        double expectedRealInputs[] = {0.0, 0.0, 0.0, 0.0, 0.0, 1.001, 1.001, 1.001, 1.001, 1.001};
        double expectedRealOutputs[] = {1.234, 1.234, 1.234, 1.234, 1.234, 3.235, 4.235, 3.235, 4.235, 3.235};
        int expectedIntInputs[] = {0, 0, 0, 0, 0, 0, 0, 2, 2, 2};
        int expectedIntOutputs[] = {2, 2, 2, 2, 2, 2, 2, 4, 5, 5};

        for (size_t i = 0; i < samplesRead; i++) {
            REQUIRE(std::fabs(realInputValues[i] - expectedRealInputs[i]) < 1.0e-9);
            REQUIRE(std::fabs(realOutputValues[i] - expectedRealOutputs[i]) < 1.0e-9);
            REQUIRE(intInputValues[i] == expectedIntInputs[i]);
            REQUIRE(intOutputValues[i] == expectedIntOutputs[i]);
        }


    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
