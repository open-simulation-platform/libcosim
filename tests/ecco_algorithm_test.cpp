#include "mock_slave.hpp"

#include <cosim/algorithm.hpp>
#include <cosim/exception.hpp>
#include <cosim/log/simple.hpp>
#include <cosim/observer/last_value_observer.hpp>
#include <cosim/observer/time_series_observer.hpp>
#include <cosim/time.hpp>

#include <exception>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>


// A helper macro to test various assertions
#include "test_require.hpp"

int main()
{
    try {
        cosim::log::setup_simple_console_logging();
        cosim::log::set_global_output_level(cosim::log::debug);

        constexpr cosim::time_point startTime;
        constexpr cosim::time_point midTime = cosim::to_time_point(15.0);

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

        {
            auto algorithm = std::make_shared<cosim::ecco_algorithm>(ecco_params, 1);
            cosim::execution execution(startTime, algorithm);
            const auto first = execution.add_slave(
                std::make_unique<mock_slave>([](double value) { return value * 2.0; }),
                "first");
            const auto second = execution.add_slave(
                std::make_unique<mock_slave>([](double value) { return value + 1.0; }),
                "second");
            const cosim::variable_id firstIn{
                first, cosim::variable_type::real, mock_slave::real_in_reference};
            const cosim::variable_id firstOut{
                first, cosim::variable_type::real, mock_slave::real_out_reference};
            const cosim::variable_id secondIn{
                second, cosim::variable_type::real, mock_slave::real_in_reference};
            const cosim::variable_id secondOut{
                second, cosim::variable_type::real, mock_slave::real_out_reference};
            execution.connect_variables(firstOut, secondIn);
            execution.connect_variables(secondOut, firstIn);
            algorithm->add_power_bond(firstIn, firstOut, secondIn, secondOut);
            execution.set_real_initial_value(first, mock_slave::real_in_reference, 1.0);
            execution.set_real_initial_value(second, mock_slave::real_in_reference, 2.0);
            for (int i = 0; i < 4; ++i) execution.step();
            const auto checkpoint = execution.export_current_state();
            REQUIRE(!algorithm->get_powerbond_energies(0).empty());
            std::stringstream stream;
            stream << cosim::serialization::format::cbor << checkpoint;
            cosim::serialization::node decoded;
            stream >> cosim::serialization::format::cbor >> decoded;
            REQUIRE(decoded == checkpoint);

            std::vector<cosim::duration> expectedSteps;
            for (int i = 0; i < 4; ++i) expectedSteps.push_back(execution.step());
            const auto expected = execution.export_current_state();
            const auto expectedEnergies = algorithm->get_powerbond_energies(0);
            execution.import_state(decoded);
            for (const auto step : expectedSteps) REQUIRE(execution.step() == step);
            const auto replayed = execution.export_current_state();
            REQUIRE(replayed.get_child("algorithm_state") == expected.get_child("algorithm_state"));
            REQUIRE(replayed.get_child("simulator_state") == expected.get_child("simulator_state"));
            REQUIRE(replayed == expected);
            REQUIRE(algorithm->get_powerbond_energies(0) == expectedEnergies);

            auto legacy = algorithm->export_current_state();
            legacy.put<std::uint32_t>("version", 1);
            algorithm->import_state(legacy);
            const auto afterLegacy = execution.export_current_state();
            REQUIRE(afterLegacy.get_child("algorithm_state") == expected.get_child("algorithm_state"));
            REQUIRE(afterLegacy == expected);

            auto invalid = algorithm->export_current_state();
            invalid.put("previous_error", -1.0);
            bool rejected = false;
            try {
                algorithm->import_state(invalid);
            } catch (const cosim::error& e) {
                rejected = e.code() == cosim::make_error_code(cosim::errc::bad_file);
            }
            REQUIRE(rejected);
            REQUIRE(execution.export_current_state() == expected);

            const auto nextStep = execution.step();
            const auto nextState = execution.export_current_state();
            execution.import_state(expected);
            legacy.erase("coordination");
            algorithm->import_state(legacy);
            REQUIRE(execution.step() == nextStep);
            REQUIRE(execution.export_current_state() == nextState);
        }

        {
            int events = 0;
            auto execution = cosim::execution(
                startTime, std::make_shared<cosim::ecco_algorithm>(ecco_params));
            execution.add_slave(
                std::make_unique<detailed_step_mock_slave>(
                    [](cosim::time_point currentT, cosim::duration deltaT) {
                        return cosim::step_result_info{
                            cosim::step_result::complete, currentT + deltaT / 2,
                            true, true, false, false};
                    },
                    [&events] { ++events; }),
                "event-slave");
            REQUIRE(execution.step() == ecco_params.step_size / 2);
            REQUIRE(execution.current_time() == startTime + ecco_params.step_size / 2);
            REQUIRE(events == 1);
        }

        {
            auto algorithm =
                std::make_shared<cosim::ecco_algorithm>(ecco_params);
            auto execution = cosim::execution(startTime, algorithm);
            execution.add_slave(
                std::make_unique<detailed_step_mock_slave>(
                    [](cosim::time_point currentT, cosim::duration deltaT) {
                        return cosim::step_result_info{
                            cosim::step_result::complete,
                            currentT + deltaT / 2,
                            true,
                            false,
                            false,
                            false};
                    }),
                "early-return");
            execution.add_slave(
                std::make_unique<detailed_step_mock_slave>(
                    [](cosim::time_point currentT, cosim::duration deltaT) {
                        return cosim::step_result_info{
                            cosim::step_result::complete,
                            currentT + deltaT,
                            false,
                            false,
                            false,
                            false};
                    }),
                "full-step");
            bool rejected = false;
            try {
                execution.step();
            } catch (const cosim::error& e) {
                rejected =
                    e.code() == cosim::make_error_code(cosim::errc::simulation_error);
            }
            REQUIRE(rejected);
        }

        {
            std::vector<cosim::duration> requestedStepSizes;
            auto algorithm =
                std::make_shared<cosim::ecco_algorithm>(ecco_params);
            auto execution = cosim::execution(startTime, algorithm);
            execution.add_slave(
                std::make_unique<detailed_step_mock_slave>(
                    [&requestedStepSizes](
                        cosim::time_point currentT,
                        cosim::duration deltaT) {
                        requestedStepSizes.push_back(deltaT);
                        return cosim::step_result_info{
                            cosim::step_result::complete,
                            currentT + deltaT / 2,
                            true,
                            false,
                            false,
                            false};
                    }),
                "early-return");
            execution.step();
            execution.step();
            execution.step();
            REQUIRE(requestedStepSizes.size() == 3);
            REQUIRE(requestedStepSizes[0] == ecco_params.step_size);
            REQUIRE(requestedStepSizes[1] == ecco_params.step_size / 2);
            REQUIRE(requestedStepSizes[2] == ecco_params.step_size / 4);
        }

        {
            const cosim::variable_value expected(
                cosim::uint64_values{{7, 11}}, {2});
            auto source =
                std::make_shared<whole_value_mock_slave>(expected);
            auto target = std::make_shared<whole_value_mock_slave>(
                cosim::variable_value(
                    cosim::uint64_values{{0, 0}}, {2}));
            auto execution = cosim::execution(
                startTime,
                std::make_shared<cosim::ecco_algorithm>(ecco_params));
            const auto sourceIndex =
                execution.add_slave(source, "whole-source");
            const auto targetIndex =
                execution.add_slave(target, "whole-target");
            execution.connect_variables(
                cosim::variable_id{
                    sourceIndex,
                    cosim::variable_type::uint64,
                    whole_value_mock_slave::output_reference},
                cosim::variable_id{
                    targetIndex,
                    cosim::variable_type::uint64,
                    whole_value_mock_slave::input_reference});
            execution.initialize();
            REQUIRE(target->input_value() == expected);
        }

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

        auto output1 = cosim::variable_id{slaves[0], cosim::variable_type::real, realOutRef};
        auto input1 = cosim::variable_id{slaves[0], cosim::variable_type::real, realInRef};
        auto output2 = cosim::variable_id{slaves[1], cosim::variable_type::real, realOutRef};
        auto input2 = cosim::variable_id{slaves[1], cosim::variable_type::real, realInRef};

        execution.connect_variables(output1, input2);
        execution.connect_variables(output2, input1);

        ecco_algo->add_power_bond(input1, output1, input2, output2);

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
