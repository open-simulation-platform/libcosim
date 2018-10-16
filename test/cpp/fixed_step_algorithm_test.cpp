#include <exception>
#include <memory>
#include <stdexcept>

#include <cse/algorithm.hpp>
#include <cse/async_slave.hpp>
#include <cse/execution.hpp>
#include <cse/log.hpp>

#include "mock_slave.hpp"


// A helper macro to test various assertions
#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)


int main()
{
    try {
        constexpr int numSlaves = 10;
        constexpr cse::time_point startTime = 0.0;
        constexpr cse::time_point midTime = 0.6;
        constexpr cse::time_point endTime = 1.0;
        constexpr cse::time_duration stepSize = 0.1;

        cse::log::set_global_output_level(cse::log::level::debug);

        // Set up execution
        auto execution = cse::execution(
            startTime,
            std::make_unique<cse::fixed_step_algorithm>(stepSize));

        auto observer = std::make_shared<cse::membuffer_observer>();
        execution.add_observer(observer);

        const cse::variable_index realOutIndex = 0;
        const cse::variable_index realInIndex = 1;

        // Add slaves to it
        for (int i = 0; i < numSlaves; ++i) {
            execution.add_slave(
                cse::make_pseudo_async(std::make_unique<mock_slave>([](double x) { return x + 1.234; })),
                "slave" + std::to_string(i));
            if (i > 0) {
                execution.connect_variables(cse::variable_id{i - 1, cse::variable_type::real, realOutIndex}, cse::variable_id{i, cse::variable_type::real, realInIndex});
            }
        }

        // Run simulation
        auto simResult = execution.simulate_until(midTime);
        REQUIRE(simResult.get());
        REQUIRE(abs(execution.current_time() - midTime) < 1.0e-6);
        simResult = execution.simulate_until(endTime);
        REQUIRE(simResult.get());

        double realOutValue = -1.0;
        double realInValue = -1.0;

        for (int j = 0; j < numSlaves; j++) {
            observer->get_real(j, gsl::make_span(&realOutIndex, 1), gsl::make_span(&realOutValue, 1));
            observer->get_real(j, gsl::make_span(&realInIndex, 1), gsl::make_span(&realInValue, 1));
            if (j > 0) {
                // Check that real input of slave j has same value as real output of slave j - 1
                printf("Slave: %d, In: %lf, Out: %lf\n", j, realInValue, realOutValue);
            }
        }
        for (int j = 0; j < numSlaves; j++) {
            double lastRealOutValue = realOutValue;
            observer->get_real(j, gsl::make_span(&realOutIndex, 1), gsl::make_span(&realOutValue, 1));
            observer->get_real(j, gsl::make_span(&realInIndex, 1), gsl::make_span(&realInValue, 1));
            if (j > 0) {
                // Check that real input of slave j has same value as real output of slave j - 1
                printf("In: %lf, Last out: %lf, Nano diff: %lf\n", realInValue, lastRealOutValue, 1.0e9 * (realInValue - lastRealOutValue));
                REQUIRE(abs(realInValue - lastRealOutValue) < 1.0e-9);
            }
        }

        const int numSamples = 11;
        double realValues[numSamples];
        cse::step_number steps[numSamples];
        observer->get_real_samples(9, realOutIndex, 0, gsl::make_span(realValues, numSamples), gsl::make_span(steps, numSamples));
        cse::step_number lastStep = -1;
        double lastValue = -1.0;
        for (int k = 0; k < numSamples; k++) {
            REQUIRE(steps[k] > lastStep);
            lastStep = steps[k];

            REQUIRE(realValues[k] > lastValue);
            lastValue = realValues[k];
        }

        constexpr cse::time_point finalTime = 5.0;

        simResult = execution.simulate_until(finalTime);
        const auto start = std::chrono::steady_clock::now();
        REQUIRE(simResult.get());
        const auto end = std::chrono::steady_clock::now();

        const auto expected = std::chrono::duration<double>(finalTime - endTime);
        const auto measured = end - start;
        const auto tolerance = std::chrono::duration<double>(0.05);
        REQUIRE(abs(measured - expected) < tolerance);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
