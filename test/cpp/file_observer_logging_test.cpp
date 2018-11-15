//
// Created by STENBRO on 11/13/2018.
//

#include <exception>
#include <memory>
#include <stdexcept>

#include <cse/algorithm.hpp>
#include <cse/async_slave.hpp>
#include <cse/execution.hpp>
#include <cse/log.hpp>

#include "mock_slave.hpp"

// A helper macro to test various assertions
#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

int main() {
    try {
        constexpr cse::time_point startTime = cse::to_time_point(0.0);
        constexpr cse::time_point endTime = cse::to_time_point(10.0);
        constexpr cse::duration stepSize = cse::to_duration(0.1);

        // Set up paths to log files based on data_dir env variable
        const char* dataDir = std::getenv("TEST_DATA_DIR");

        std::string csv_log_path(dataDir);
        std::string bin_log_path(dataDir);

        csv_log_path.append("log.txt");
        bin_log_path.append("log.bin");

        cse::log::set_global_output_level(cse::log::level::debug);

        // Set up the execution
        auto execution = cse::execution(startTime, std::make_unique<cse::fixed_step_algorithm>(stepSize));

        // Set up and add file observers of csv and binary format to the execution
        auto csv_observer = std::make_shared<cse::file_observer>(csv_log_path, false);
        auto bin_observer = std::make_shared<cse::file_observer>(bin_log_path, true);
        cse::simulator_index sim_index_1 = execution.add_observer(csv_observer);
        cse::simulator_index sim_index_2 = execution.add_observer(bin_observer);

        // This is not needed, sim_index_1 = sim_index_2
        (void)sim_index_2;

        // Add a slave to the execution and connect variables
        execution.add_slave(
                cse::make_pseudo_async(std::make_unique<mock_slave>([](double x) { return x + 1.234; })), "slave");

        // Run the simulation
        auto simResult = execution.simulate_until(endTime);
        REQUIRE(simResult.get());

        // Log the observed results
        csv_observer->write_real_samples(sim_index_1);
        bin_observer->write_real_samples(sim_index_1);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}