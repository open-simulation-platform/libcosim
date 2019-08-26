#include "mock_slave.hpp"

#include <cse/algorithm.hpp>
#include <cse/async_slave.hpp>
#include <cse/execution.hpp>
#include <cse/log/simple.hpp>
#include <cse/observer/time_series_observer.hpp>

#include <exception>
#include <memory>
#include <stdexcept>

#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

int main()
{
    try {
        cse::log::setup_simple_console_logging();
        cse::log::set_global_output_level(cse::log::debug);

        constexpr cse::time_point startTime = cse::to_time_point(0.0);
        constexpr cse::time_point midTime = cse::to_time_point(1.0);
        constexpr cse::time_point endTime = cse::to_time_point(2.0);
        constexpr cse::duration stepSize = cse::to_duration(0.1);

        auto algorithm = std::make_shared<cse::fixed_step_algorithm>(stepSize);
        auto execution = cse::execution(startTime, algorithm);

        auto observer = std::make_shared<cse::time_series_observer>();
        execution.add_observer(observer);

        double x1 = 0;
        auto simIndex1 = execution.add_slave(
            cse::make_pseudo_async(std::make_unique<mock_slave>([&x1](double x) { return x + x1++; })), "slave uno");

        double x2 = 0;
        auto simIndex2 = execution.add_slave(
            cse::make_pseudo_async(std::make_unique<mock_slave>([&x2](double x) { return x + x2++; })), "slave dos");

        algorithm->set_stepsize_decimation_factor(simIndex2, 2);

        auto variableId1 = cse::variable_id{simIndex1, cse::variable_type::real, 0};
        auto variableId2 = cse::variable_id{simIndex2, cse::variable_type::real, 0};

        observer->start_observing(variableId1);

        // Run the simulation
        auto simResult = execution.simulate_until(midTime);
        REQUIRE(simResult.get());

        observer->start_observing(variableId2);

        simResult = execution.simulate_until(endTime);
        REQUIRE(simResult.get());

        const int numSamples = 20;
        const cse::variable_index varIndex = 0;
        double realValues1[numSamples];
        double realValues2[numSamples];

        size_t samplesRead = observer->get_synchronized_real_series(simIndex1, varIndex, simIndex2, varIndex, 5, realValues1, realValues2);
        REQUIRE(samplesRead == 5);

        double expectedReals1[5] = {12.0, 14.0, 16.0, 18.0, 20.0};
        double expectedReals2[5] = {6.0, 7.0, 8.0, 9.0, 10.0};

        for (size_t i = 0; i < samplesRead; i++) {
            REQUIRE(std::fabs(realValues1[i] - expectedReals1[i]) < 1.0e9);
            REQUIRE(std::fabs(realValues2[i] - expectedReals2[i]) < 1.0e9);
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
