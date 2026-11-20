#include "mock_slave.hpp"

#include <cosim/algorithm.hpp>
#include <cosim/execution.hpp>
#include <cosim/fmi/fmu.hpp>
#include <cosim/fmi/importer.hpp>
#include <cosim/fs_portability.hpp>
#include <cosim/log/simple.hpp>
#include <cosim/observer/file_observer.hpp>
#include "../src/cosim/utility/filesystem.hpp"

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>


// A helper macro to test various assertions
#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

void verify_independent_default_logging()
{
    const auto* referenceDir = std::getenv("REFERENCE_FMU_V2_DIR");
    REQUIRE(referenceDir != nullptr);
    cosim::utility::temp_dir logDirectory;
    auto importer = cosim::fmi::importer::create();
    auto fmu = importer->import(
        cosim::filesystem::path(referenceDir) / "Dahlquist.fmu");
    auto execution = cosim::execution(
        cosim::to_time_point(0.0),
        std::make_unique<cosim::fixed_step_algorithm>(cosim::to_duration(0.1)));
    auto observer = std::make_shared<cosim::file_observer>(logDirectory.path());
    execution.add_observer(observer);
    execution.add_slave(fmu->instantiate_slave("causality"), "causality");
    execution.step();
    observer->stop_recording();

    bool foundCsv = false;
    for (const auto& entry : cosim::filesystem::directory_iterator(logDirectory.path())) {
        if (entry.path().extension() != ".csv") continue;
        REQUIRE(!foundCsv);
        foundCsv = true;
        std::ifstream csv(entry.path());
        REQUIRE(csv);
        std::string header;
        REQUIRE(std::getline(csv, header));
        std::stringstream headerStream(header);
        std::vector<std::string> columns;
        std::string column;
        while (std::getline(headerStream, column, ',')) columns.push_back(column);
        REQUIRE(std::find(columns.begin(), columns.end(), "time") != columns.end());
        REQUIRE(std::find(columns.begin(), columns.end(), "der(x)") == columns.end());
        std::string sample;
        REQUIRE(std::getline(csv, sample));
        REQUIRE(!sample.empty());
    }
    REQUIRE(foundCsv);
}

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
        auto simResult = execution.simulate_until(endTime);
        REQUIRE(simResult);

        // Print the log paths
        std::cout << "Log directory: " << csv_observer->get_log_path() << std::endl;

        verify_independent_default_logging();

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
