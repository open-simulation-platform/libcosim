#include "mock_slave.hpp"

#include <cosim/algorithm.hpp>
#include <cosim/execution.hpp>
#include <cosim/fs_portability.hpp>
#include <cosim/log/simple.hpp>
#include <cosim/observer/file_observer.hpp>

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

        constexpr cosim::time_point startTime = cosim::to_time_point(0.0);
        constexpr cosim::time_point endTime = cosim::to_time_point(10.0);
        constexpr cosim::duration stepSize = cosim::to_duration(0.1);

        const auto logPath = cosim::filesystem::current_path() / "logs";
        cosim::filesystem::path csvPath = cosim::filesystem::path(logPath);

        // Set up the execution
        auto execution = cosim::execution(startTime, std::make_unique<cosim::fixed_step_algorithm>(stepSize));

        // Set up and add file observer to the execution
        auto csv_observer = std::make_shared<cosim::file_observer>(csvPath);
        execution.add_observer(csv_observer);

        // Add a slave to the execution and connect variables
        execution.add_slave(
            std::make_unique<mock_slave>([](double x) { return x + 1.234; }), "slave uno");
        execution.add_slave(
            std::make_unique<mock_slave>([](double x) { return x + 1.234; },
                [](int y) { return y - 4; },
                [](bool z) { return !z; }),
            "slave dos");

        // Run the simulation
        execution.simulate_until(endTime);

        // Print the log paths
        std::cout << "Log directory: " << csv_observer->get_log_path() << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
