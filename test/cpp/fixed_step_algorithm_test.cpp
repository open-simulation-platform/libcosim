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
        cse::log::setup_simple_console_logging();
        cse::log::set_global_output_level(cse::log::debug);

        constexpr int numSlaves = 10;
        constexpr cse::time_point startTime;
        constexpr cse::time_point midTime = cse::to_time_point(0.6);
        constexpr cse::time_point endTime = cse::to_time_point(1.0);
        constexpr cse::duration stepSize = cse::to_duration(0.1);

        // Set up execution
        auto execution = cse::execution(
            startTime,
            std::make_unique<cse::fixed_step_algorithm>(stepSize));

        // Default should not be real time
        REQUIRE(!execution.is_real_time_simulation());

        auto observer = std::make_shared<cse::last_value_observer>();
        execution.add_observer(observer);

        const cse::value_reference realOutRef = 0;
        const cse::value_reference realInRef = 1;


        // Add slaves to it
        for (int i = 0; i < numSlaves; ++i) {
            execution.add_slave(
                cse::make_pseudo_async(std::make_unique<mock_slave>([](double x) { return x + 1.234; })),
                "slave" + std::to_string(i));
            if (i > 0) {
                execution.add_connection(
                    std::make_shared<cse::scalar_connection>(
                        cse::variable_id{i - 1, cse::variable_type::real, realOutRef},
                        cse::variable_id{i, cse::variable_type::real, realInRef}));
            }
        }

        auto observer2 = std::make_shared<cse::time_series_observer>();
        execution.add_observer(observer2);
        observer2->start_observing(cse::variable_id{9, cse::variable_type::real, realOutRef});

        // Run simulation
        auto simResult = execution.simulate_until(midTime);
        auto start = std::chrono::steady_clock::now();
        REQUIRE(simResult.get());
        REQUIRE(std::chrono::abs(execution.current_time() - midTime) < std::chrono::microseconds(1));
        REQUIRE(execution.get_measured_real_time_factor() > 1.0);
        simResult = execution.simulate_until(endTime);
        REQUIRE(simResult.get());
        auto end = std::chrono::steady_clock::now();

        auto simulatedDuration = endTime - startTime;
        auto measuredDuration = end - start;
        bool fasterThanRealTime = measuredDuration < simulatedDuration;
        REQUIRE(fasterThanRealTime);

        double realOutValue = -1.0;
        double realInValue = -1.0;

        for (int j = 0; j < numSlaves; j++) {
            double lastRealOutValue = realOutValue;
            observer->get_real(j, gsl::make_span(&realOutRef, 1), gsl::make_span(&realOutValue, 1));
            observer->get_real(j, gsl::make_span(&realInRef, 1), gsl::make_span(&realInValue, 1));
            if (j > 0) {
                // Check that real input of slave j has same value as real output of slave j - 1
                REQUIRE(std::fabs(realInValue - lastRealOutValue) < 1.0e-9);
            }
        }

        const int numSamples = 10;
        double realValues[numSamples];
        cse::step_number steps[numSamples];
        cse::time_point timeValues[numSamples];
        observer2->get_real_samples(9, realOutRef, 1, gsl::make_span(realValues, numSamples), gsl::make_span(steps, numSamples), gsl::make_span(timeValues, numSamples));
        cse::step_number lastStep = -1;
        double lastValue = -1.0;
        for (int k = 0; k < numSamples; k++) {
            REQUIRE(steps[k] > lastStep);
            lastStep = steps[k];

            REQUIRE(realValues[k] > lastValue);
            lastValue = realValues[k];

            if (k > 0) {
                cse::duration diff = timeValues[k] - timeValues[k - 1];
                REQUIRE(diff == stepSize);
            }
        }

        constexpr auto finalTime = cse::to_time_point(5.0);

        execution.enable_real_time_simulation();
        simResult = execution.simulate_until(finalTime);
        start = std::chrono::steady_clock::now();
        REQUIRE(simResult.get());
        end = std::chrono::steady_clock::now();

        simulatedDuration = finalTime - endTime;
        measuredDuration = end - start;
        const auto tolerance = std::chrono::milliseconds(50);

        fasterThanRealTime = (simulatedDuration - measuredDuration) > tolerance;
        bool slowerThanRealTime = (measuredDuration - simulatedDuration) > tolerance;
        REQUIRE(!slowerThanRealTime);
        REQUIRE(!fasterThanRealTime);

        printf("Real time factor: %lf\n", execution.get_measured_real_time_factor());
        REQUIRE(fabs(execution.get_measured_real_time_factor() - 1.0) < 0.05);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
