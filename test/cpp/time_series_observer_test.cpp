#include <exception>
#include <memory>
#include <stdexcept>

#include <cse/algorithm.hpp>
#include <cse/async_slave.hpp>
#include <cse/execution.hpp>
#include <cse/log.hpp>

#include "mock_slave.hpp"

#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

int main()
{
    try {
        constexpr cse::time_point startTime = cse::to_time_point(0.0);
        constexpr cse::time_point time1 = cse::to_time_point(1.0);
        constexpr cse::time_point time2 = cse::to_time_point(2.0);
        constexpr cse::time_point endTime = cse::to_time_point(3.0);
        constexpr cse::duration stepSize = cse::to_duration(0.1);

        cse::log::set_global_output_level(cse::log::level::debug);

        // Set up the execution
        auto execution = cse::execution(startTime, std::make_unique<cse::fixed_step_algorithm>(stepSize));

        // Set up the observer and add to the execution
        auto observer = std::make_shared<cse::time_series_observer>();
        execution.add_observer(observer);

        // Add slaves to the execution and connect variables
        auto simIndex = execution.add_slave(
            cse::make_pseudo_async(std::make_unique<mock_slave>([](double x) { return x + 1.234; })), "slave uno");

        auto variableId = cse::variable_id{simIndex, cse::variable_type::real, 0};

        // Run the simulation
        auto simResult = execution.simulate_until(time1);
        REQUIRE(simResult.get());

        const int numSamples = 30;
        const cse::variable_index varIndex = 0;
        double realValues[numSamples];
        cse::step_number steps[numSamples];
        cse::time_point times[numSamples];

        size_t samplesRead = observer->get_real_samples(simIndex, varIndex, 0, gsl::make_span(realValues, numSamples), gsl::make_span(steps, numSamples), gsl::make_span(times, numSamples));
        REQUIRE(samplesRead == 0);

        observer->start_observing(variableId);

        simResult = execution.simulate_until(time2);
        REQUIRE(simResult.get());

        samplesRead = observer->get_real_samples(simIndex, varIndex, 0, gsl::make_span(realValues, numSamples), gsl::make_span(steps, numSamples), gsl::make_span(times, numSamples));
        REQUIRE(samplesRead == 10);

        double expectedReals[] = {1.234, 1.234, 1.234, 1.234, 1.234, 1.234, 1.234, 1.234, 1.234, 1.234};
        int counter = 0;
        for (size_t i = 0; i < samplesRead; i++) {
            REQUIRE(std::fabs(expectedReals[i] - realValues[i]) < 1.0e-9);
            counter++;
        }
        for (size_t i = 0; i < samplesRead; i++) {
            if (i > 0) {
                REQUIRE(times[i] > times[i - 1]);
            }
        }

        observer->stop_observing(variableId);

        simResult = execution.simulate_until(endTime);
        REQUIRE(simResult.get());

        samplesRead = observer->get_real_samples(simIndex, varIndex, 0, gsl::make_span(realValues, numSamples), gsl::make_span(steps, numSamples), gsl::make_span(times, numSamples));
        REQUIRE(samplesRead == 0);
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
