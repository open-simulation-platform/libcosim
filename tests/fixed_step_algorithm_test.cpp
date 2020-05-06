#include "mock_slave.hpp"

#include <cse/algorithm.hpp>
#include <cse/async_slave.hpp>
#include <cse/execution.hpp>
#include <cse/log/simple.hpp>
#include <cse/observer/last_value_observer.hpp>
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
        cosim::log::setup_simple_console_logging();
        cosim::log::set_global_output_level(cosim::log::debug);

        constexpr int numSlaves = 10;
        constexpr cosim::time_point startTime;
        constexpr cosim::time_point midTime = cosim::to_time_point(0.6);
        constexpr cosim::time_point endTime = cosim::to_time_point(1.0);
        constexpr cosim::duration stepSize = cosim::to_duration(0.05);

        // Set up execution
        auto execution = cosim::execution(
            startTime,
            std::make_unique<cosim::fixed_step_algorithm>(stepSize));

        // Default should not be real time
        REQUIRE(!execution.is_real_time_simulation());

        auto observer = std::make_shared<cosim::last_value_observer>();
        execution.add_observer(observer);

        const cosim::value_reference realOutRef = mock_slave::real_out_reference;
        const cosim::value_reference realInRef = mock_slave::real_in_reference;


        // Add slaves to it
        std::vector<cosim::simulator_index> slaves;
        slaves.push_back(
            execution.add_slave(
                cosim::make_pseudo_async(
                    std::make_unique<mock_slave>([](cosim::time_point t, double) {
                        return cosim::to_double_time_point(t);
                    })),
                "clock_slave"));
        for (int i = 1; i < numSlaves; ++i) {
            slaves.push_back(
                execution.add_slave(
                    cosim::make_pseudo_async(
                        std::make_unique<mock_slave>([](double x) {
                            return x + 1.234;
                    })),
                    "adder_slave" + std::to_string(i)));
            execution.connect_variables(
                cosim::variable_id{slaves[i - 1], cosim::variable_type::real, realOutRef},
                cosim::variable_id{slaves[i], cosim::variable_type::real, realInRef});
        }

        // Add an observer that watches the last slave
        auto observer2 = std::make_shared<cosim::time_series_observer>();
        execution.add_observer(observer2);
        observer2->start_observing(
            cosim::variable_id{slaves.back(), cosim::variable_type::real, realOutRef});

        // Run simulation
        auto simResult = execution.simulate_until(midTime);
        REQUIRE(simResult.get());
        REQUIRE(std::chrono::abs(execution.current_time() - midTime) < std::chrono::microseconds(1));
        // Actual performance should not be tested here - just check that we get a positive value
        REQUIRE(execution.get_measured_real_time_factor() > 0.0);
        simResult = execution.simulate_until(endTime);
        REQUIRE(simResult.get());

        // Check that time, step number and output values increase monotonically
        const int numSamples = 10;
        double realValues[numSamples];
        cosim::step_number steps[numSamples];
        cosim::time_point timeValues[numSamples];
        observer2->get_real_samples(
            slaves.back(),
            realOutRef,
            numSlaves, // changes won't propagate to the last slave until the numSlaves'th step
            gsl::make_span(realValues, numSamples),
            gsl::make_span(steps, numSamples),
            gsl::make_span(timeValues, numSamples));

        for (int k = 1; k < numSamples; k++) {
            REQUIRE(steps[k] > steps[k-1]);
            REQUIRE(realValues[k] > realValues[k-1]);
            REQUIRE(timeValues[k] - timeValues[k-1] == stepSize);
        }

        // Run for another period with an RTF target > 1
        constexpr auto finalTime = cosim::to_time_point(2.0);
        constexpr double rtfTarget = 2.25;
        execution.enable_real_time_simulation();
        execution.set_real_time_factor_target(rtfTarget);
        simResult = execution.simulate_until(finalTime);
        REQUIRE(simResult.get());
        REQUIRE(std::fabs(execution.get_real_time_factor_target() - rtfTarget) < 1.0e-9);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
