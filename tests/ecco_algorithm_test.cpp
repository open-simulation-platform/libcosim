#include "mock_slave.hpp"

#include <cosim/algorithm.hpp>
#include <cosim/log/simple.hpp>
#include <cosim/observer/last_value_observer.hpp>
#include <cosim/observer/time_series_observer.hpp>
#include <cosim/time.hpp>

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
        constexpr cosim::time_point midTime = cosim::to_time_point(15.0);

        auto ecco_params = cosim::ecco_parameters{
            0.99,
            cosim::to_duration(0.0001),
            cosim::to_duration(0.00001),
            cosim::to_duration(0.01),
            0.2,
            1.5,
            1e-6,
            1e-6,
            0.2,
            0.15};

        // Set up an ecco algo
        auto ecco_algo = std::make_shared<cosim::ecco_algorithm>(ecco_params);

        // Set up execution
        auto execution = cosim::execution(
            startTime, ecco_algo);


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
                std::make_unique<mock_slave>([&x1](cosim::time_point currentTime, cosim::duration currentStepSize, double x) {
                    auto dt = 1e-4;
                    const auto tEnd = cosim::to_double_duration(currentStepSize, currentTime);
                    for (double t = 0.0; t < tEnd; t += dt) {
                        if (t + dt > tEnd) {
                            dt = tEnd - t;
                        }
                        auto dx = -2.0 * x1 + x;
                        x1 += dx * dt;
                    }
                    return x1;
                }),
                "A"));

        double x2 = 5;
        slaves.push_back(
            execution.add_slave(
                std::make_unique<mock_slave>([&x2](cosim::time_point currentTime, cosim::duration currentStepSize, double x) {
                    auto dt = 1e-4;
                    const auto tEnd = cosim::to_double_duration(currentStepSize, currentTime);
                    for (double t = 0.0; t < tEnd; t += dt) {
                        if (t + dt > tEnd) {
                            dt = tEnd - t;
                        }
                        auto dx = -x2 + x;
                        x2 += dx * dt;
                    }
                    return x2;
                }),
                "B"));

        auto u1 = cosim::variable_id{slaves[0], cosim::variable_type::real, realOutRef};
        auto y1 = cosim::variable_id{slaves[1], cosim::variable_type::real, realInRef};
        auto u2 = cosim::variable_id{slaves[1], cosim::variable_type::real, realOutRef};
        auto y2 = cosim::variable_id{slaves[0], cosim::variable_type::real, realInRef};

        execution.connect_variables(u1, y1);
        execution.connect_variables(u2, y2);

        ecco_algo->add_variable_pairs(std::make_pair(u1, u2), std::make_pair(y1, y2));

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

        cosim::step_number stepNums[2];
        t_observer->get_step_numbers(slaves[0], {}, midTime, gsl::make_span(stepNums, 2));
        const auto numSamples = stepNums[1] - stepNums[0];
        // const int numSamples = 11;
        std::vector<double> real_a_input(numSamples);
        std::vector<double> real_b_input(numSamples);
        std::vector<double> real_a_output(numSamples);
        std::vector<double> real_b_output(numSamples);
        std::vector<cosim::step_number> steps(numSamples);
        std::vector<cosim::time_point> timeValues(numSamples);

        t_observer->get_real_samples(
            slaves[0],
            realInRef,
            0,
            gsl::make_span(real_a_input),
            gsl::make_span(steps),
            gsl::make_span(timeValues));

        t_observer->get_real_samples(
            slaves[0],
            realOutRef,
            0,
            gsl::make_span(real_a_output),
            gsl::make_span(steps),
            gsl::make_span(timeValues));

        t_observer->get_real_samples(
            slaves[1],
            realInRef,
            0,
            gsl::make_span(real_b_input),
            gsl::make_span(steps),
            gsl::make_span(timeValues));

        t_observer->get_real_samples(
            slaves[1],
            realOutRef,
            0,
            gsl::make_span(real_b_output),
            gsl::make_span(steps),
            gsl::make_span(timeValues));

        std::cout << "time,stepsize,a_in,a_out,b_in,b_out" << std::endl;
        for (int i = 1; i < numSamples; ++i) {
            std::cout << cosim::to_double_time_point(timeValues[i])
                      << "," << cosim::to_double_duration(timeValues[i] - timeValues[i - 1], timeValues[i - 1])
                      << "," << real_a_input[i]
                      << "," << real_a_output[i]
                      << "," << real_b_input[i]
                      << "," << real_b_output[i]
                      << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
