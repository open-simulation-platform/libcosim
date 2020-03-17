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
        constexpr cse::time_point endTime = cse::to_time_point(1.1);
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

        observer->start_observing(
            cse::variable_id{simIndex, cse::variable_type::real, mock_slave::real_in_reference});
        observer->start_observing(
            cse::variable_id{simIndex, cse::variable_type::real, mock_slave::real_out_reference});
        observer->start_observing(
            cse::variable_id{simIndex, cse::variable_type::integer, mock_slave::integer_in_reference});
        observer->start_observing(
            cse::variable_id{simIndex, cse::variable_type::integer, mock_slave::integer_out_reference});

        constexpr bool input = true;
        constexpr bool output = false;

        auto realModifier1 = cse::scenario::real_modifier{[](double original, cse::duration) { return original + 1.001; }};
        auto realAction1 = cse::scenario::variable_action{simIndex, mock_slave::real_in_reference, realModifier1, input};
        auto realEvent1 = cse::scenario::event{cse::to_time_point(0.5), realAction1};

        auto realModifier2 = cse::scenario::real_modifier{[](double /*original*/, cse::duration) { return -1.0; }};
        auto realAction2 = cse::scenario::variable_action{simIndex, mock_slave::real_out_reference, realModifier2, output};
        auto realEvent2 = cse::scenario::event{cse::to_time_point(0.2), realAction2};

        auto realModifier3 = cse::scenario::real_modifier{nullptr};
        auto realAction3 = cse::scenario::variable_action{simIndex, mock_slave::real_out_reference, realModifier3, output};
        auto realEvent3 = cse::scenario::event{cse::to_time_point(0.3), realAction3};

        auto intModifier1 = cse::scenario::integer_modifier{[](int /*original*/, cse::duration) { return 2; }};
        auto intAction1 = cse::scenario::variable_action{simIndex, mock_slave::integer_in_reference, intModifier1, input};
        auto intEvent1 = cse::scenario::event{cse::to_time_point(0.65), intAction1};

        auto intModifier2 = cse::scenario::integer_modifier{[](int /*original*/, cse::duration) { return 5; }};
        auto intAction2 = cse::scenario::variable_action{simIndex, mock_slave::integer_out_reference, intModifier2, output};
        auto intEvent2 = cse::scenario::event{cse::to_time_point(0.8), intAction2};

        auto events = std::vector<cse::scenario::event>();
        events.push_back(realEvent1);
        events.push_back(realEvent2);
        events.push_back(realEvent3);
        events.push_back(intEvent1);
        events.push_back(intEvent2);

        auto end = cse::to_time_point(1.0);

        auto scenario = cse::scenario::scenario{events, end};

        scenarioManager->load_scenario(scenario, startTime);

        auto simResult = execution.simulate_until(endTime);
        REQUIRE(simResult.get());

        const int numSamples = 11;
        double realInputValues[numSamples];
        double realOutputValues[numSamples];
        int intInputValues[numSamples];
        int intOutputValues[numSamples];
        cse::step_number steps[numSamples];
        cse::time_point times[numSamples];

        size_t samplesRead = observer->get_real_samples(
            simIndex,
            mock_slave::real_in_reference,
            1,
            gsl::make_span(realInputValues, numSamples),
            gsl::make_span(steps, numSamples),
            gsl::make_span(times, numSamples));
        REQUIRE(samplesRead == 11);
        samplesRead = observer->get_real_samples(
            simIndex,
            mock_slave::real_out_reference,
            1,
            gsl::make_span(realOutputValues, numSamples),
            gsl::make_span(steps, numSamples),
            gsl::make_span(times, numSamples));
        REQUIRE(samplesRead == 11);
        samplesRead = observer->get_integer_samples(
            simIndex,
            mock_slave::integer_in_reference,
            1,
            gsl::make_span(intInputValues, numSamples),
            gsl::make_span(steps, numSamples),
            gsl::make_span(times, numSamples));
        REQUIRE(samplesRead == 11);
        samplesRead = observer->get_integer_samples(
            simIndex,
            mock_slave::integer_out_reference,
            1,
            gsl::make_span(intOutputValues, numSamples),
            gsl::make_span(steps, numSamples),
            gsl::make_span(times, numSamples));
        REQUIRE(samplesRead == 11);

        double expectedRealInputs[] = {0.0, 0.0, 0.0, 0.0, 0.0, 1.001, 1.001, 1.001, 1.001, 1.001, 0.0};
        double expectedRealOutputs[] = {1.234, 1.234, -1.0, 1.234, 1.234, 2.235, 2.235, 2.235, 2.235, 2.235, 1.234};
        int expectedIntInputs[] = {0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 0};
        int expectedIntOutputs[] = {2, 2, 2, 2, 2, 2, 2, 4, 5, 5, 2};

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
