#include "mock_slave.hpp"

#include <cosim/algorithm.hpp>
#include <cosim/log/simple.hpp>
#include <cosim/observer/last_value_observer.hpp>
#include <cosim/observer/time_series_observer.hpp>

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

        constexpr cosim::time_point startTime;
        constexpr cosim::time_point midTime = cosim::to_time_point(1.0);
        constexpr cosim::duration stepSize = cosim::to_duration(0.1);

        auto ecco_params = cosim::ecco_parameters{
            0.8,
            cosim::to_duration(0.001),
            cosim::to_duration(0.0001),
            cosim::to_duration(0.01),
            0.2,
            1.5,
            1e-5,
            1e-5,
            0.2,
            0.15};

        // Set up execution
        auto execution = cosim::execution(
            startTime,
            std::make_unique<cosim::ecco_algorithm>(ecco_params));


        // Default should not be real time
        const auto realTimeConfig = execution.get_real_time_config();
        REQUIRE(!realTimeConfig->real_time_simulation);

        auto lv_observer = std::make_shared<cosim::last_value_observer>();
        execution.add_observer(lv_observer);

        const cosim::value_reference realOutRef = mock_slave::real_out_reference;
        const cosim::value_reference realInRef = mock_slave::real_in_reference;


        // Add slaves to it
        std::vector<cosim::simulator_index> slaves;
        double x1 = 1;
        slaves.push_back(
            execution.add_slave(
                std::make_unique<mock_slave>([&x1, stepSize](double x) {
                    auto dx = -2 * x1 + x;
                    x1 += dx * cosim::to_double_duration(stepSize, {});
                    return x1;
                }),
                "A"));

        double x2 = 5;
        slaves.push_back(
            execution.add_slave(
                std::make_unique<mock_slave>([&x2, stepSize](double x) {
                    auto dx = -5 * x2 + x;
                    x2 += dx * cosim::to_double_duration(stepSize, {});
                    return x2;
                }),
                "B"));

        execution.connect_variables(
            cosim::variable_id{slaves[0], cosim::variable_type::real, realOutRef},
            cosim::variable_id{slaves[1], cosim::variable_type::real, realInRef});
        execution.connect_variables(
            cosim::variable_id{slaves[1], cosim::variable_type::real, realOutRef},
            cosim::variable_id{slaves[0], cosim::variable_type::real, realInRef});

        execution.set_real_initial_value(slaves[0], realInRef, 0.5);

        // Add an observer that watches the last slave
        auto t_observer = std::make_shared<cosim::time_series_observer>();
        execution.add_observer(t_observer);
        t_observer->start_observing(cosim::variable_id{slaves[0], cosim::variable_type::real, realInRef});
        t_observer->start_observing(cosim::variable_id{slaves[0], cosim::variable_type::real, realOutRef});
        t_observer->start_observing(cosim::variable_id{slaves[1], cosim::variable_type::real, realInRef});
        t_observer->start_observing(cosim::variable_id{slaves[1], cosim::variable_type::real, realOutRef});

        // Run simulation
        auto simResult = execution.simulate_until(midTime);
        REQUIRE(simResult);

        const int numSamples = 11;
        double real_a_input[numSamples];
        double real_b_input[numSamples];
        double real_a_output[numSamples];
        double real_b_output[numSamples];
        cosim::step_number steps[numSamples];
        cosim::time_point timeValues[numSamples];

        t_observer->get_real_samples(
            slaves[0],
            realInRef,
            0,
            gsl::make_span(real_a_input, numSamples),
            gsl::make_span(steps, numSamples),
            gsl::make_span(timeValues, numSamples));

        t_observer->get_real_samples(
            slaves[0],
            realOutRef,
            0,
            gsl::make_span(real_a_output, numSamples),
            gsl::make_span(steps, numSamples),
            gsl::make_span(timeValues, numSamples));

        t_observer->get_real_samples(
            slaves[1],
            realInRef,
            0,
            gsl::make_span(real_b_input, numSamples),
            gsl::make_span(steps, numSamples),
            gsl::make_span(timeValues, numSamples));

        t_observer->get_real_samples(
            slaves[1],
            realOutRef,
            0,
            gsl::make_span(real_b_output, numSamples),
            gsl::make_span(steps, numSamples),
            gsl::make_span(timeValues, numSamples));

        for (int i = 0; i < numSamples; ++i) {
            std::cout << i << " | "
                      << real_a_input[i] << " | "
                      << real_a_output[i] << " | "
                      << real_b_input[i] << " | "
                      << real_b_output[i] << " | "
                      << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
