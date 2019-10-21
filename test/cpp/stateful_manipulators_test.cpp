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

        observer->start_observing(cse::variable_id{simIndex, cse::variable_type::real, 0});
        observer->start_observing(cse::variable_id{simIndex, cse::variable_type::real, 1});

        constexpr bool input = true;
        constexpr bool output = false;

        auto realModifier1 = cse::scenario::time_dependent_real_modifier{
            startTime,
            [](double original, cse::time_point time) {
                (void)time;
                return original * 2.0; }};
        auto realAction1 = cse::scenario::variable_action{simIndex, 0, realModifier1, output};
        auto realEvent1 = cse::scenario::event{cse::to_time_point(0.5), realAction1};

        auto events = std::vector<cse::scenario::event>();
        events.push_back(realEvent1);

        auto end = cse::to_time_point(1.0);

        auto scenario = cse::scenario::scenario{events, end};

        scenarioManager->load_scenario(scenario, startTime);

        auto simResult = execution.simulate_until(endTime);
        /*REQUIRE(simResult.get());

        const int numSamples = 11;
        double realInputValues[numSamples];
        double realOutputValues[numSamples];
        cse::step_number steps[numSamples];
        cse::time_point times[numSamples];

        size_t samplesRead = observer->get_real_samples(simIndex, 1, 1, gsl::make_span(realInputValues, numSamples), gsl::make_span(steps, numSamples), gsl::make_span(times, numSamples));
        REQUIRE(samplesRead == 11);
        samplesRead = observer->get_real_samples(simIndex, 0, 1, gsl::make_span(realOutputValues, numSamples), gsl::make_span(steps, numSamples), gsl::make_span(times, numSamples));
        REQUIRE(samplesRead == 11);

        double expectedRealOutputs[] = {1.234, 1.234, 1.234, 1.234, 1.234, 1.234, 1.234, 1.234, 1.234, 1.234, 1.234};

        for (size_t i = 0; i < samplesRead; i++) {
            std::cout << realOutputValues[i] << " ";
            //REQUIRE(std::fabs(realOutputValues[i] - expectedRealOutputs[i]) < 1.0e-9);
        }*/

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
