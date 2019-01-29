#include "mock_slave.hpp"

#include <cse/algorithm.hpp>
#include <cse/async_slave.hpp>
#include <cse/execution.hpp>
#include <cse/log.hpp>
#include <cse/observer/file_observer.hpp>

#include <boost/filesystem.hpp>

#include <exception>
#include <memory>
#include <stdexcept>


// A helper macro to test various assertions
#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

int main()
{
    try {
        constexpr cse::time_point startTime = cse::to_time_point(0.0);
        constexpr cse::time_point endTime = cse::to_time_point(10.0);
        constexpr cse::duration stepSize = cse::to_duration(0.1);

        const auto logPath = boost::filesystem::current_path() / "logs";
        boost::filesystem::path csvPath = boost::filesystem::path(logPath);
        //boost::filesystem::path binPath = boost::filesystem::path(logPath);

        cse::log::set_global_output_level(cse::log::level::debug);

        // Set up the execution
        auto execution = cse::execution(startTime, std::make_unique<cse::fixed_step_algorithm>(stepSize));

        // Set up and add file observers of csv and binary format to the execution
        auto csv_observer = std::make_shared<cse::file_observer>(csvPath, false, 50);
        //auto bin_observer = std::make_shared<cse::file_observer>(binPath, true, 50);
        execution.add_observer(csv_observer);
        //execution.add_observer(bin_observer);

        // Add a slave to the execution and connect variables
        execution.add_slave(
            cse::make_pseudo_async(std::make_unique<mock_slave>([](double x) { return x + 1.234; })), "slave uno");
        execution.add_slave(
            cse::make_pseudo_async(std::make_unique<mock_slave>([](double x) { return x + 1.234; },
                [](int y) { return y - 4; },
                [](bool z) { return !z; })),
            "slave dos");

        // Run the simulation
        auto simResult = execution.simulate_until(endTime);
        REQUIRE(simResult.get());

        // Print the log paths
        std::cout << "CSV file: " << csv_observer->get_log_path() << std::endl;
        //std::cout << "BIN file: " << bin_observer->get_log_path() << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
