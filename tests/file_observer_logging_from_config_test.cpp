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

        const auto testDataDir = std::getenv("TEST_DATA_DIR");
        REQUIRE(testDataDir);
        cosim::filesystem::path configPath = cosim::filesystem::path(testDataDir) / "LogConfig.xml";

        const auto logPath = cosim::filesystem::current_path() / "logs";
        cosim::filesystem::path csvPath = cosim::filesystem::path(logPath);


        // Set up the execution and add observer
        auto execution = cosim::execution(startTime, std::make_unique<cosim::fixed_step_algorithm>(stepSize));
        auto csv_config = cosim::file_observer_config::parse(configPath);
        auto csv_observer = std::make_shared<cosim::file_observer>(csvPath, csv_config);
        execution.add_observer(csv_observer);

        // Add two slaves to the execution and connect variables
        execution.add_slave(
            std::make_unique<mock_slave>(
                [](double x) { return x + 1.234; },
                [](int x) { return x + 1; },
                [](bool x) { return x; },
                [](std::string_view) { return std::string("hello log"); }),
            "slave");

        execution.add_slave(
            std::make_unique<mock_slave>(
                [](double x) { return x + 123.456; },
                [](int x) { return x - 1; }),
            "slave1");

        execution.add_slave(
            std::make_unique<mock_slave>(
                [](double x) { return x + 1.234; },
                [](int x) { return x + 1; }),
            "slave2");

        // Run the simulation
        auto simResult = execution.simulate_until(endTime);
        REQUIRE(simResult);

        REQUIRE(cosim::filesystem::exists(cosim::filesystem::path(csvPath / "slave.csv")));
        REQUIRE(cosim::filesystem::exists(cosim::filesystem::path(csvPath / "slave2.csv")));

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
