#include "mock_slave.hpp"

#include <cosim/algorithm.hpp>
#include <cosim/async_slave.hpp>
#include <cosim/execution.hpp>
#include <cosim/log/simple.hpp>
#include <cosim/observer/time_series_observer.hpp>

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
        cosim::log::setup_simple_console_logging();
        cosim::log::set_global_output_level(cosim::log::debug);

        constexpr int numSlaves = 2;
        constexpr cosim::time_point startTime;
        constexpr cosim::time_point midTime = cosim::to_time_point(1.0);
        constexpr cosim::duration stepSize = cosim::to_duration(0.1);

        // Set up execution
        auto execution = cosim::execution(
            startTime,
            std::make_unique<cosim::fixed_step_algorithm>(stepSize));

        auto observer = std::make_shared<cosim::time_series_observer>();
        execution.add_observer(observer);

        const cosim::value_reference realOutIndex = 0;
        const cosim::value_reference realInIndex = 1;

        // Add slaves to it
        for (int i = 0; i < numSlaves; ++i) {
            execution.add_slave(
                cosim::make_pseudo_async(std::make_unique<mock_slave>([](double x) { return x + 1.234; })),
                "slave" + std::to_string(i));
            if (i > 0) {
                execution.connect_variables(
                    cosim::variable_id{i - 1, cosim::variable_type::real, realOutIndex},
                    cosim::variable_id{i, cosim::variable_type::real, realInIndex});
            }
        }

        // Run simulation
        auto simResult = execution.simulate_until(midTime);
        REQUIRE(simResult.get());

        cosim::step_number stepNumbers1[2];
        observer->get_step_numbers(0, cosim::to_duration(0.5), gsl::make_span(stepNumbers1, 2));
        REQUIRE(stepNumbers1[0] == 5);
        REQUIRE(stepNumbers1[1] == 10);

        cosim::step_number stepNumbers2[2];
        observer->get_step_numbers(0, cosim::to_time_point(0.4), cosim::to_time_point(0.9), gsl::make_span(stepNumbers2, 2));
        REQUIRE(stepNumbers2[0] == 4);
        REQUIRE(stepNumbers2[1] == 9);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
