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

        // Add slaves to it
        for (int i = 0; i < numSlaves; ++i) {
            execution.add_slave(
                cse::make_pseudo_async(std::make_unique<mock_slave>()),
                "slave" + std::to_string(i));
        }

        // Run simulation
        auto simResult = execution.simulate_until(midTime);
        REQUIRE(simResult.get());
        REQUIRE(execution.current_time() == midTime);
        simResult = execution.simulate_until(endTime);
        REQUIRE(simResult.get());

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
