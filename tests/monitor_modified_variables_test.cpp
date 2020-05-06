#include "mock_slave.hpp"

#include "cosim/algorithm.hpp"
#include "cosim/async_slave.hpp"
#include "cosim/execution.hpp"
#include "cosim/log/simple.hpp"
#include "cosim/manipulator/override_manipulator.hpp"
#include "cosim/observer/time_series_observer.hpp"

#include <exception>
#include <memory>
#include <stdexcept>

#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

int main()
{
    try {
        cosim::log::setup_simple_console_logging();
        cosim::log::set_global_output_level(cosim::log::trace);

        constexpr cosim::time_point startTime = cosim::to_time_point(0.0);
        constexpr cosim::time_point endTime = cosim::to_time_point(1.0);
        constexpr cosim::duration stepSize = cosim::to_duration(0.1);

        auto execution = cosim::execution(startTime, std::make_unique<cosim::fixed_step_algorithm>(stepSize));

        auto observer = std::make_shared<cosim::time_series_observer>();
        auto manipulator = std::make_shared<cosim::override_manipulator>();
        execution.add_observer(observer);
        execution.add_manipulator(manipulator);

        auto simIndex = execution.add_slave(
            cosim::make_pseudo_async(std::make_unique<mock_slave>(
                [](double x) { return x + 1.234; },
                [](int y) { return y + 2; })),
            "Slave");

        observer->start_observing(cosim::variable_id{simIndex, cosim::variable_type::integer, mock_slave::integer_out_reference});

        manipulator->override_integer_variable(simIndex, mock_slave::integer_out_reference, 1);

        auto simResult = execution.simulate_until(endTime);
        REQUIRE(simResult.get());

        const int numSamples = 10;
        int intOutputValues[numSamples];
        cosim::step_number steps[numSamples];
        cosim::time_point times[numSamples];

        size_t samplesRead = observer->get_integer_samples(simIndex, mock_slave::integer_out_reference, 1, gsl::make_span(intOutputValues, numSamples), gsl::make_span(steps, numSamples), gsl::make_span(times, numSamples));
        REQUIRE(samplesRead == 10);

        for (size_t i = 0; i < samplesRead; i++) {
            REQUIRE(intOutputValues[i] == 1);
        }

        auto modifiedVariables = execution.get_modified_variables();
        REQUIRE(modifiedVariables.size() == 1);

        for (auto var : modifiedVariables) {
            REQUIRE(var.type == cosim::variable_type::integer);
            REQUIRE(var.reference == 0);
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
