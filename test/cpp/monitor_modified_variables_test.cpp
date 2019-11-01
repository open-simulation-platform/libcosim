#include "mock_slave.hpp"

#include "cse/algorithm.hpp"
#include "cse/async_slave.hpp"
#include "cse/execution.hpp"
#include "cse/log/simple.hpp"
#include "cse/manipulator/override_manipulator.hpp"
#include "cse/observer/time_series_observer.hpp"

#include <exception>
#include <memory>
#include <stdexcept>

#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

int main()
{
    try {
        cse::log::setup_simple_console_logging();
        cse::log::set_global_output_level(cse::log::trace);

        constexpr cse::time_point startTime = cse::to_time_point(0.0);
        constexpr cse::time_point endTime = cse::to_time_point(1.0);
        constexpr cse::duration stepSize = cse::to_duration(0.1);

        auto execution = cse::execution(startTime, std::make_unique<cse::fixed_step_algorithm>(stepSize));

        auto observer = std::make_shared<cse::time_series_observer>();
        auto manipulator = std::make_shared<cse::override_manipulator>();
        execution.add_observer(observer);
        execution.add_manipulator(manipulator);

        auto simIndex = execution.add_slave(
            cse::make_pseudo_async(std::make_unique<mock_slave>(
                [](double x) { return x + 1.234; },
                [](int y) { return y + 2; })),
            "Slave");

        observer->start_observing(cse::variable_id{simIndex, cse::variable_type::integer, 0});

        manipulator->override_integer_variable(simIndex, 0, 1);

        auto simResult = execution.simulate_until(endTime);
        REQUIRE(simResult.get());

        const int numSamples = 10;
        int intOutputValues[numSamples];
        cse::step_number steps[numSamples];
        cse::time_point times[numSamples];

        size_t samplesRead = observer->get_integer_samples(simIndex, 0, 1, gsl::make_span(intOutputValues, numSamples), gsl::make_span(steps, numSamples), gsl::make_span(times, numSamples));
        REQUIRE(samplesRead == 10);

        for (size_t i = 0; i < samplesRead; i++) {
            REQUIRE(intOutputValues[i] == 1);
        }

        auto modifiedVariables = execution.get_modified_variables();
        REQUIRE(modifiedVariables.size() == 1);

        for (auto var : modifiedVariables) {
            REQUIRE(var.type == cse::variable_type::integer);
            REQUIRE(var.reference == 0);
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
