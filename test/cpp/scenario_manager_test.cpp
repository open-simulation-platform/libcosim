#include "mock_slave.hpp"

#include "cse/algorithm.hpp"
#include "cse/async_slave.hpp"
#include "cse/execution.hpp"
#include "cse/log.hpp"
#include "cse/observer/time_series_observer.hpp"

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
        observer->start_observing(cse::variable_id{simIndex, cse::variable_type::real, 1});
        observer->start_observing(cse::variable_id{simIndex, cse::variable_type::integer, 0});
        observer->start_observing(cse::variable_id{simIndex, cse::variable_type::integer, 1});

        auto realTrigger1 = cse::scenario::time_trigger{cse::to_time_point(0.5)};
        auto realManipulator1 = cse::scenario::real_input_manipulator{[](double original) { return original + 1.001; }};
        auto realAction1 = cse::scenario::variable_action{simIndex, 1, realManipulator1};
        auto realEvent1 = cse::scenario::event{realTrigger1, realAction1};

        auto realTrigger2 = cse::scenario::time_trigger{cse::to_time_point(0.2)};
        auto realManipulator2 = cse::scenario::real_output_manipulator{[](double /*original*/) { return -1.0; }};
        auto realAction2 = cse::scenario::variable_action{simIndex, 0, realManipulator2};
        auto realEvent2 = cse::scenario::event{realTrigger2, realAction2};

        auto realTrigger3 = cse::scenario::time_trigger{cse::to_time_point(0.3)};
        auto realManipulator3 = cse::scenario::real_output_manipulator{nullptr};
        auto realAction3 = cse::scenario::variable_action{simIndex, 0, realManipulator3};
        auto realEvent3 = cse::scenario::event{realTrigger3, realAction3};

        auto intTrigger1 = cse::scenario::time_trigger{cse::to_time_point(0.65)};
        auto intManipulator1 = cse::scenario::integer_input_manipulator{[](int /*original*/) { return 2; }};
        auto intAction1 = cse::scenario::variable_action{simIndex, 1, intManipulator1};
        auto intEvent1 = cse::scenario::event{intTrigger1, intAction1};

        auto intTrigger2 = cse::scenario::time_trigger{cse::to_time_point(0.8)};
        auto intManipulator2 = cse::scenario::integer_output_manipulator{[](int /*original*/) { return 5; }};
        auto intAction2 = cse::scenario::variable_action{simIndex, 0, intManipulator2};
        auto intEvent2 = cse::scenario::event{intTrigger2, intAction2};

        auto events = std::vector<cse::scenario::event>();
        events.push_back(realEvent1);
        events.push_back(realEvent2);
        events.push_back(realEvent3);
        events.push_back(intEvent1);
        events.push_back(intEvent2);
        auto scenario = cse::scenario::scenario{events};

        scenarioManager->load_scenario(scenario, startTime);

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
        double expectedRealOutputs[] = {1.234, 1.234, -1.0, 1.234, 1.234, 2.235, 2.235, 2.235, 2.235, 2.235};
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
