#include "mock_slave.hpp"

#include <cosim/algorithm.hpp>
#include <cosim/execution.hpp>
#include <cosim/log/simple.hpp>
#include <cosim/observer/last_value_observer.hpp>
#include <cosim/osp_config_parser.hpp>
#include <cosim/serialization.hpp>

#include <boost/property_tree/json_parser.hpp>

#include <chrono>
#include <exception>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>


// A helper macro to test various assertions
#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

// Helper function to get observed values from multiple simulators.
std::vector<double> get_reals(
    cosim::last_value_observer& observer,
    const std::vector<cosim::simulator_index>& simulators,
    cosim::value_reference valueRef)
{
    auto values = std::vector<double>(
        simulators.size(), std::numeric_limits<double>::quiet_NaN());
    for (std::size_t i = 0; i < simulators.size(); ++i) {
        observer.get_real(
            simulators[i],
            gsl::make_span(&valueRef, 1),
            gsl::make_span(values.data() + i, 1));
    }
    return values;
}

int main()
{
    try {
        cosim::log::setup_simple_console_logging();
        cosim::log::set_global_output_level(cosim::log::debug);

        // ================================================================
        // == Reference FMU test - Dahlquist (for byte vectors)
        // ================================================================
        constexpr cosim::duration stepSize = cosim::to_duration(0.05);

        const auto testDataDir = std::getenv("TEST_DATA_DIR");
        REQUIRE(testDataDir);
        auto configPath = cosim::filesystem::path(testDataDir) / "msmi" / "OspSystemStructure_Dahlquist.xml";

        auto resolver = cosim::default_model_uri_resolver();
        const auto config = cosim::load_osp_config(configPath, *resolver);
        auto algorithm_cfg = std::get<cosim::fixed_step_algorithm_params>(config.algorithm_configuration);
        auto execution = cosim::execution(
            config.start_time,
            std::make_shared<cosim::fixed_step_algorithm>(algorithm_cfg));

        const auto entityMaps = cosim::inject_system_structure(
            execution, config.system_structure, config.initial_values);

        auto obs = std::make_shared<cosim::last_value_observer>();
        execution.add_observer(obs);

        const auto timeRef = 0;
        const auto xRef = 1;
        const auto velocityRef = 2;

        execution.simulate_until(cosim::to_time_point(0.5));
        auto timeValues = get_reals(*obs, {0}, timeRef);
        auto xValues = get_reals(*obs, {0}, xRef);
        auto velocityValues = get_reals(*obs, {0}, velocityRef);


        const auto state_bb = execution.export_current_state();

        // Export state
        std::ofstream outFile("state_bb.bin", std::ios::binary);
        outFile << cosim::serialization::format::cbor << state_bb;
        outFile.close();

        execution.simulate_until(cosim::to_time_point(0.5));

        // import state
        std::ifstream inFile("state_bb.bin", std::ios::binary);
        cosim::serialization::node state_bb_imported;
        inFile >> cosim::serialization::format::cbor >> state_bb_imported;
        inFile.close();

        execution.import_state(state_bb_imported);
        auto state_bb_2 = execution.export_current_state();

        auto timeValues2 = get_reals(*obs, {0}, timeRef);
        auto xValues2 = get_reals(*obs, {0}, xRef);
        auto velocityValues2 = get_reals(*obs, {0}, velocityRef);

        REQUIRE(timeValues == timeValues2);
        REQUIRE(xValues == xValues2);
        REQUIRE(velocityValues == velocityValues2);

        REQUIRE(state_bb_2 == state_bb_imported);
        REQUIRE(state_bb == state_bb_imported);


        // ================================================================
        // == Mockup tests
        // ================================================================
        constexpr int simulatorCount = 10;
        constexpr cosim::time_point time0;
        constexpr cosim::time_point time1 = cosim::to_time_point(0.6);
        constexpr cosim::time_point time2 = cosim::to_time_point(1.0);

        // Set up execution
        execution = cosim::execution(
            time0,
            std::make_unique<cosim::fixed_step_algorithm>(stepSize, 1));

        auto observer = std::make_shared<cosim::last_value_observer>();
        execution.add_observer(observer);

        const cosim::value_reference realOutRef = mock_slave::real_out_reference;
        const cosim::value_reference realInRef = mock_slave::real_in_reference;

        // Add and connect subsimulators
        std::vector<cosim::simulator_index> simulators;
        simulators.push_back(
            execution.add_slave(
                std::make_unique<mock_slave>([](cosim::time_point t, cosim::duration, double) {
                    return cosim::to_double_time_point(t);
                }),
                "clock"));
        for (int i = 1; i < simulatorCount; ++i) {
            simulators.push_back(
                execution.add_slave(
                    std::make_unique<mock_slave>([](double x) {
                        return x + 1.234;
                    }),
                    "adder" + std::to_string(i)));
            execution.connect_variables(
                cosim::variable_id{simulators[i - 1], cosim::variable_type::real, realOutRef},
                cosim::variable_id{simulators[i], cosim::variable_type::real, realInRef});
        }

        // Save initial state
        execution.initialize();
        auto state0Values = get_reals(*observer, simulators, realOutRef);
        const auto state0 = execution.export_current_state();

        // Write state0 to a file
        outFile = std::ofstream("state0.bin", std::ios::binary);
        outFile << cosim::serialization::format::cbor << state0;
        outFile.close();

        // Advance to time1 and save state again
        execution.simulate_until(time1);
        auto state1Values = get_reals(*observer, simulators, realOutRef);
        const auto state1 = execution.export_current_state();

        // Write state1 to a file
        outFile = std::ofstream("state1.bin", std::ios::binary);
        outFile << cosim::serialization::format::cbor << state1;
        outFile.close();
        REQUIRE(state1Values > state0Values);

        // Advance to time2 and save state again
        execution.simulate_until(time2);
        auto state2Values = get_reals(*observer, simulators, realOutRef);
        const auto state2 = execution.export_current_state();

        // Write state2 to a file
        outFile = std::ofstream("state2.bin", std::ios::binary);
        outFile << cosim::serialization::format::cbor << state2;
        outFile.close();
        REQUIRE(state2Values > state1Values);

        // Restore state0 from file and compare values
        cosim::serialization::node state0_a;
        inFile = std::ifstream("state0.bin", std::ios::binary);
        inFile >> cosim::serialization::format::cbor >> state0_a;
        inFile.close();
        REQUIRE(state0_a == state0);

        execution.import_state(state0_a);

        REQUIRE(execution.current_time() == time0);
        auto state0ValuesAgain = get_reals(*observer, simulators, realOutRef);
        REQUIRE(state0ValuesAgain == state0Values);

        // Advance to time1 again and compare values
        cosim::serialization::node state1_a;
        inFile = std::ifstream("state1.bin", std::ios::binary);
        inFile >> cosim::serialization::format::cbor >> state1_a;
        inFile.close();
        REQUIRE(state1_a == state1);

        execution.simulate_until(time1);
        auto state1ValuesAgain = get_reals(*observer, simulators, realOutRef);
        REQUIRE(state1ValuesAgain == state1Values);

        // Restore state2 from file and compare values
        cosim::serialization::node state2_a;
        inFile = std::ifstream("state2.bin", std::ios::binary);
        inFile >> cosim::serialization::format::cbor >> state2_a;
        inFile.close();
        REQUIRE(state2_a == state2);

        execution.import_state(state2_a);
        REQUIRE(execution.current_time() == time2);
        auto state2ValuesAgain = get_reals(*observer, simulators, realOutRef);
        REQUIRE(state2ValuesAgain == state2Values);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
