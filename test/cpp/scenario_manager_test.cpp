#include "mock_slave.hpp"

#include <cse/algorithm.hpp>
#include <cse/async_slave.hpp>
#include <cse/execution.hpp>
#include <cse/log.hpp>
#include <cse/observer/membuffer_observer.hpp>

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
        constexpr cse::time_point endTime = cse::to_time_point(2.0);
        constexpr cse::duration stepSize = cse::to_duration(0.2);

        cse::log::set_global_output_level(cse::log::level::trace);

        auto execution = cse::execution(startTime, std::make_unique<cse::fixed_step_algorithm>(stepSize));

        auto observer = std::make_shared<cse::membuffer_observer>();
        execution.add_observer(observer);
        auto scenarioManager = std::make_shared<cse::scenario_manager>();
        execution.add_manipulator(scenarioManager);


        auto simIndex = execution.add_slave(
            cse::make_pseudo_async(std::make_unique<mock_slave>(
                [](double x) { return x + 1.234; },
                [](int y) { return y + 2; })),
            "slave uno");

        auto realTrigger = cse::scenario::time_trigger{cse::to_time_point(1.0)};
        auto realInput = cse::variable_id{simIndex, cse::variable_type::real, 1};
        auto realAction = cse::scenario::variable_action{realInput, 1.001};
        auto realEvent = cse::scenario::event{123, realTrigger, realAction};

        auto intTrigger = cse::scenario::time_trigger{cse::to_time_point(1.3)};
        auto intInput = cse::variable_id{simIndex, cse::variable_type::integer, 1};
        auto intAction = cse::scenario::variable_action{intInput, 2};
        auto intEvent = cse::scenario::event{456, intTrigger, intAction};

        auto events = std::vector<cse::scenario::event>();
        events.push_back(realEvent);
        events.push_back(intEvent);
        auto scenario = cse::scenario::scenario{events};

        scenarioManager->load_scenario(scenario, startTime);

        auto simResult = execution.simulate_until(endTime);
        REQUIRE(simResult.get());

        const cse::variable_index varIndex = 0;
        const int numSamples = 11;
        double realValues[numSamples];
        cse::step_number steps[numSamples];
        cse::time_point times[numSamples];

        size_t samplesRead = observer->get_real_samples(simIndex, varIndex, 0, gsl::make_span(realValues, numSamples), gsl::make_span(steps, numSamples), gsl::make_span(times, numSamples));
        REQUIRE(samplesRead == 11);
        int intValues[numSamples];
        samplesRead = observer->get_integer_samples(simIndex, varIndex, 0, gsl::make_span(intValues, numSamples), gsl::make_span(steps, numSamples), gsl::make_span(times, numSamples));
        REQUIRE(samplesRead == 11);

        double expectedReals[] = {0, 1.234, 1.234, 1.234, 1.234, 1.234, 2.235, 2.235, 2.235, 2.235, 2.235};
        int expectedInts[] = {0, 2, 2, 2, 2, 2, 2, 2, 4, 4, 4};

        for (size_t i = 0; i < samplesRead; i++) {
            REQUIRE(std::fabs(realValues[i] - expectedReals[i]) < 1.0e-9);
            REQUIRE(intValues[i] == expectedInts[i]);
        }


    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
