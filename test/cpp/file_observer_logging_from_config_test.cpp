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

        const auto testDataDir = std::getenv("TEST_DATA_DIR");
        REQUIRE(testDataDir);
        boost::filesystem::path configPath = boost::filesystem::path(testDataDir) / "LogConfig.xml";

        const auto logPath = boost::filesystem::current_path() / "logs";
        boost::filesystem::path csvPath = boost::filesystem::path(logPath);

        cse::log::set_global_output_level(cse::log::level::debug);

        // Set up the execution and add observer
        auto execution = cse::execution(startTime, std::make_unique<cse::fixed_step_algorithm>(stepSize));
        auto csv_observer = std::make_shared<cse::file_observer>(configPath, csvPath);
        execution.add_observer(csv_observer);

        // Add a slave to the execution and connect variables
        execution.add_slave(
            cse::make_pseudo_async(std::make_unique<mock_slave>(
                [](double x) { return x + 1.234; },
                [](int x) { return x + 1; })),
            "slave");

        // Run the simulation
        auto simResult = execution.simulate_until(endTime);
        REQUIRE(simResult.get());

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
