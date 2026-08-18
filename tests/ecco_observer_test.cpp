#include "mock_slave.hpp"

#include <cosim/algorithm.hpp>
#include <cosim/log/simple.hpp>
#include <cosim/observer/ecco_observer.hpp>
#include <cosim/time.hpp>

#include <cmath>
#include <exception>
#include <memory>
#include <stdexcept>
#include <string>


// A helper macro to test various assertions
#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

int main()
{
    try {
        cosim::log::setup_simple_console_logging();
        cosim::log::set_global_output_level(cosim::log::warning);

        constexpr cosim::time_point startTime;
        constexpr cosim::time_point endTime = cosim::to_time_point(1.0);

        auto ecco_params = cosim::ecco_algorithm_params{
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

        auto ecco_algo = std::make_shared<cosim::ecco_algorithm>(ecco_params);
        auto execution = cosim::execution(startTime, ecco_algo);

        const cosim::value_reference realOutRef = mock_slave::real_out_reference;
        const cosim::value_reference realInRef = mock_slave::real_in_reference;

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

        auto output1 = cosim::variable_id{slaves[0], cosim::variable_type::real, realOutRef};
        auto input1 = cosim::variable_id{slaves[0], cosim::variable_type::real, realInRef};
        auto output2 = cosim::variable_id{slaves[1], cosim::variable_type::real, realOutRef};
        auto input2 = cosim::variable_id{slaves[1], cosim::variable_type::real, realInRef};

        execution.connect_variables(output1, input2);
        execution.connect_variables(output2, input1);

        ecco_algo->add_power_bond("bond", input1, output1, input2, output2);
        execution.set_real_initial_value(slaves[0], realInRef, 0.5);

        // Observe the power bond states with the ecco_observer.
        auto eccoObserver = std::make_shared<cosim::ecco_observer>(ecco_algo);
        execution.add_observer(eccoObserver);

        auto simResult = execution.simulate_until(endTime);
        REQUIRE(simResult);

        // Polling the observer must yield the same bond that was configured.
        const auto states = eccoObserver->get_power_bond_states();
        REQUIRE(states.size() == 1);
        REQUIRE(states.count("bond") == 1);

        const auto bondState = eccoObserver->get_power_bond_state("bond");

        // The polled state must match the one exposed by the observer.
        REQUIRE(bondState.time == states.at("bond").time);
        REQUIRE(bondState.power_a == states.at("bond").power_a);
        REQUIRE(bondState.power_b == states.at("bond").power_b);

        // Power and residual must be consistent.
        REQUIRE(bondState.power_a == bondState.input_a_value * bondState.output_a_value);
        REQUIRE(bondState.power_b == bondState.input_b_value * bondState.output_b_value);
        REQUIRE(bondState.power_residual == std::abs(bondState.power_a - bondState.power_b));

        // Querying an unknown bond must throw.
        bool threw = false;
        try {
            eccoObserver->get_power_bond_state("does_not_exist");
        } catch (const std::out_of_range&) {
            threw = true;
        }
        REQUIRE(threw);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
