#include "mock_slave.hpp"

#include <cse/algorithm.hpp>
#include <cse/async_slave.hpp>
#include <cse/execution.hpp>
#include <cse/log/simple.hpp>
#include <cse/observer/time_series_observer.hpp>

#include <cmath>
#include <exception>
#include <memory>
#include <stdexcept>


// A helper macro to test various assertions
#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)


int main()
{
    try {
        cse::log::setup_simple_console_logging();
        cse::log::set_global_output_level(cse::log::debug);

        constexpr int numSlaves = 2;
        constexpr cse::time_point startTime;
        constexpr cse::time_point midTime = cse::to_time_point(1.0);
        constexpr cse::duration stepSize = cse::to_duration(0.1);

        // Set up execution
        auto execution = cse::execution(
            startTime,
            std::make_unique<cse::fixed_step_algorithm>(stepSize));

        auto observer = std::make_shared<cse::time_series_observer>();
        execution.add_observer(observer);

        const cse::variable_index realOutIndex = 0;
        const cse::variable_index realInIndex = 1;

        // Add slaves to it
        for (int i = 0; i < numSlaves; ++i) {
            execution.add_slave(
                cse::make_pseudo_async(std::make_unique<mock_slave>([](double x) { return x + 1.234; })),
                "slave" + std::to_string(i));
            if (i > 0) {
                execution.add_connection(
                    std::make_shared<cse::scalar_connection>(
                        cse::variable_id{i - 1, cse::variable_type::real, realOutIndex},
                        cse::variable_id{i, cse::variable_type::real, realInIndex}));
            }
        }

        // Run simulation
        auto simResult = execution.simulate_until(midTime);
        REQUIRE(simResult.get());

        cse::step_number stepNumbers1[2];
        observer->get_step_numbers(0, cse::to_duration(0.5), gsl::make_span(stepNumbers1, 2));
        REQUIRE(stepNumbers1[0] == 5);
        REQUIRE(stepNumbers1[1] == 10);

        cse::step_number stepNumbers2[2];
        observer->get_step_numbers(0, cse::to_time_point(0.4), cse::to_time_point(0.9), gsl::make_span(stepNumbers2, 2));
        REQUIRE(stepNumbers2[0] == 4);
        REQUIRE(stepNumbers2[1] == 9);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
