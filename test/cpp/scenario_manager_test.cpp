#include "mock_slave.hpp"

#include <cse/algorithm.hpp>
#include <cse/async_slave.hpp>
#include <cse/execution.hpp>
#include <cse/log.hpp>
#include <cse/observer/membuffer_observer.hpp>
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
        observer->start_observing(cse::variable_id{simIndex, cse::variable_type::integer, 0});
        observer->start_observing(cse::variable_id{simIndex, cse::variable_type::integer, 1});

        auto realTrigger = cse::scenario::time_trigger{cse::to_time_point(0.5)};
        auto realAction = cse::scenario::variable_action{simIndex, cse::variable_causality::input, 1, 1.001};
        auto realEvent = cse::scenario::event{123, realTrigger, realAction};

        auto intTrigger1 = cse::scenario::time_trigger{cse::to_time_point(0.65)};
        auto intAction1 = cse::scenario::variable_action{simIndex, cse::variable_causality::input, 1, 2};
        auto intEvent1 = cse::scenario::event{456, intTrigger1, intAction1};

        auto intTrigger2 = cse::scenario::time_trigger{cse::to_time_point(0.8)};
        auto intAction2 = cse::scenario::variable_action{simIndex, cse::variable_causality::output, 0, 5};
        auto intEvent2 = cse::scenario::event{789, intTrigger2, intAction2};

        auto events = std::vector<cse::scenario::event>();
        events.push_back(realEvent);
        events.push_back(intEvent1);
        events.push_back(intEvent2);
        auto scenario = cse::scenario::scenario{events};

        scenarioManager->load_scenario(scenario, startTime);

        auto simResult = execution.simulate_until(endTime);
        REQUIRE(simResult.get());

        const int numSamples = 10;
        double realValues[numSamples];
        int intInputValues[numSamples];
        int intOutputValues[numSamples];
        cse::step_number steps[numSamples];
        cse::time_point times[numSamples];

        size_t samplesRead = observer->get_real_samples(simIndex, 0, 1, gsl::make_span(realValues, numSamples), gsl::make_span(steps, numSamples), gsl::make_span(times, numSamples));
        REQUIRE(samplesRead == 10);
        samplesRead = observer->get_integer_samples(simIndex, 1, 1, gsl::make_span(intInputValues, numSamples), gsl::make_span(steps, numSamples), gsl::make_span(times, numSamples));
        REQUIRE(samplesRead == 10);
        samplesRead = observer->get_integer_samples(simIndex, 0, 1, gsl::make_span(intOutputValues, numSamples), gsl::make_span(steps, numSamples), gsl::make_span(times, numSamples));
        REQUIRE(samplesRead == 10);

        double expectedReals[] = {1.234, 1.234, 1.234, 1.234, 1.234, 2.235, 2.235, 2.235, 2.235, 2.235};
        int expectedIntInputs[] = {0, 0, 0, 0, 0, 0, 0, 2, 2, 2};
        int expectedIntOutputs[] = {2, 2, 2, 2, 2, 2, 2, 4, 5, 5};

        for (size_t i = 0; i < samplesRead; i++) {
            REQUIRE(std::fabs(realValues[i] - expectedReals[i]) < 1.0e-9);
            REQUIRE(intInputValues[i] == expectedIntInputs[i]);
            REQUIRE(intOutputValues[i] == expectedIntOutputs[i]);
        }


    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
