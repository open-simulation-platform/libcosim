#include "mock_slave.hpp"

#include <cse/algorithm.hpp>
#include <cse/async_slave.hpp>
#include <cse/execution.hpp>
#include <cse/log.hpp>
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
        constexpr cse::time_point startTime;
        constexpr cse::time_point endTime = cse::to_time_point(1.0);
        constexpr cse::duration stepSize = cse::to_duration(0.1);

        cse::log::set_global_output_level(cse::log::level::debug);

        auto algorithm = std::make_shared<cse::fixed_step_algorithm>(stepSize);

        auto execution = cse::execution(
            startTime,
            algorithm);

        auto observer = std::make_shared<cse::last_value_observer>();
        execution.add_observer(observer);

        const cse::variable_index realOutIndex = 0;
        const cse::variable_index realInIndex = 1;

        double v = 0.0;

        auto idx0 = execution.add_slave(cse::make_pseudo_async(std::make_unique<mock_slave>([&v](double /*x*/) { return ++v; })), "slave 0");
        auto idx1 = execution.add_slave(cse::make_pseudo_async(std::make_unique<mock_slave>()), "slave 1");

        execution.connect_variables(
            cse::variable_id{0, cse::variable_type::real, realOutIndex},
            cse::variable_id{1, cse::variable_type::real, realInIndex});

        algorithm->set_stepsize_decimation_factor(idx0, 1);
        algorithm->set_stepsize_decimation_factor(idx1, 2);

        auto observer2 = std::make_shared<cse::time_series_observer>();
        execution.add_observer(observer2);
        observer2->start_observing(cse::variable_id{0, cse::variable_type::real, realOutIndex});
        observer2->start_observing(cse::variable_id{1, cse::variable_type::real, realOutIndex});

        // Run simulation
        auto simResult = execution.simulate_until(endTime);
        REQUIRE(simResult.get());

        const int numSamples = 10;
        double realValues0[numSamples];
        cse::step_number steps0[numSamples];
        cse::time_point timeValues0[numSamples];
        observer2->get_real_samples(0, realOutIndex, 1, gsl::make_span(realValues0, numSamples), gsl::make_span(steps0, numSamples), gsl::make_span(timeValues0, numSamples));

        double realValues1[numSamples];
        cse::step_number steps1[numSamples];
        cse::time_point timeValues1[numSamples];
        observer2->get_real_samples(1, realOutIndex, 1, gsl::make_span(realValues1, numSamples), gsl::make_span(steps1, numSamples), gsl::make_span(timeValues1, numSamples));

        double expectedReals0[numSamples] = {1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0};
        double expectedReals1[numSamples] = {1.0, 1.0, 2.0, 2.0, 4.0, 4.0, 6.0, 6.0, 8.0, 8.0};

        for (int i = 0; i < numSamples; ++i) {
            std::cout << "Slave 0 actual: " << realValues0[i] << ", expected: " << expectedReals0[i] << ", diff: " << std::fabs(expectedReals0[i] - realValues0[i]) << std::endl;
        }
        for (int i = 0; i < numSamples; ++i) {
            std::cout << "Slave 1 actual: " << realValues1[i] << ", expected: " << expectedReals1[i] << ", diff: " << std::fabs(expectedReals1[i] - realValues1[i]) << std::endl;
        }

        for (int k = 0; k < numSamples; k++) {
            //            std::cout << "Slave 0 actual: " << realValues0[k] << ", expected: " << expectedReals0[k] << ", diff: " << std::fabs(expectedReals0[k] - realValues0[k]) << std::endl;
            REQUIRE(std::fabs(expectedReals0[k] - realValues0[k]) < 1e-9);
            REQUIRE(std::fabs(expectedReals1[k] - realValues1[k]) < 1e-9);
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
