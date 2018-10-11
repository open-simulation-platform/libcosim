#include <exception>
#include <memory>
#include <stdexcept>

#include <cse/algorithm.hpp>
#include <cse/async_slave.hpp>
#include <cse/execution.hpp>

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
        REQUIRE(execution.current_time() == midTime);
        simResult = execution.simulate_until(endTime);
        REQUIRE(simResult.get());

        double realOutValue = -1.0;
        double realInValue = -1.0;

        for (int j = 0; j < numSlaves; j++) {
            double lastRealOutValue = realOutValue;
            observer->get_real(j, gsl::make_span(&realOutIndex, 1), gsl::make_span(&realOutValue, 1));
            observer->get_real(j, gsl::make_span(&realInIndex, 1), gsl::make_span(&realInValue, 1));
            if (j > 0) {
                // Check that real input of slave j has same value as real output of slave j - 1
                REQUIRE(realInValue == lastRealOutValue);
            }
        }

        // TODO: Check values of connected signal chain!

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
