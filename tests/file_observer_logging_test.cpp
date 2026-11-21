#include "mock_slave.hpp"

#include <cosim/algorithm.hpp>
#include <cosim/execution.hpp>
#include <cosim/fmi/fmu.hpp>
#include <cosim/fmi/importer.hpp>
#include <cosim/fs_portability.hpp>
#include <cosim/log/simple.hpp>
#include <cosim/observer/file_observer.hpp>
#include "../src/cosim/utility/filesystem.hpp"

#include <boost/date_time/posix_time/posix_time.hpp>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <memory>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>


// A helper macro to test various assertions
#include "test_require.hpp"

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

    const std::regex timestampedFilename(
        "causality_[0-9]{8}_[0-9]{6}_[0-9]{" +
        std::to_string(boost::posix_time::time_duration::num_fractional_digits()) +
        "}\\.csv");
    bool foundCsv = false;
    for (const auto& entry : cosim::filesystem::directory_iterator(logDirectory.path())) {
        if (entry.path().extension() != ".csv") continue;
        REQUIRE(!foundCsv);
        foundCsv = true;
        REQUIRE(std::regex_match(entry.path().filename().string(), timestampedFilename));
        REQUIRE(cosim::filesystem::exists(
            logDirectory.path() / (entry.path().stem().string() + "_metadata.yaml")));
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

class byte_metadata_mock_slave : public mock_slave
{
public:
    cosim::model_description model_description() const override
    {
        cosim::model_description model;
        model.name = "byte_metadata_mock";
        model.uuid = "3b04d3f0-7248-49c7-a700-33a89e002d2d";
        cosim::variable_description variable{};
        variable.name = "signed8";
        variable.reference = 10;
        variable.type = cosim::variable_type::int8;
        variable.causality = cosim::variable_causality::output;
        variable.variability = cosim::variable_variability::discrete;
        variable.start = std::int8_t{-120};
        model.variables.push_back(variable);
        variable.name = "unsigned8";
        variable.reference = 11;
        variable.type = cosim::variable_type::uint8;
        variable.start = std::uint8_t{250};
        model.variables.push_back(variable);
        return model;
    }

    std::vector<cosim::variable_value> get_variable_values(
        gsl::span<const cosim::typed_value_reference> variables) const override
    {
        std::vector<cosim::variable_value> values;
        for (const auto variable : variables) {
            if (variable.type == cosim::variable_type::int8 && variable.reference == 10) {
                values.emplace_back(cosim::int8_values{{-120}});
            } else if (variable.type == cosim::variable_type::uint8 && variable.reference == 11) {
                values.emplace_back(cosim::uint8_values{{250}});
            } else {
                throw std::invalid_argument("Invalid byte variable");
            }
        }
        return values;
    }
};

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

        cosim::utility::temp_dir blockedLogDirectory;
        const auto blockedMetadata = blockedLogDirectory.path() / "blocked_metadata.yaml";
        cosim::filesystem::create_directory(blockedMetadata);
        cosim::file_observer_config blockedConfig;
        blockedConfig.set_timestamped_filenames(false)
            .log_all_simulator_variables("blocked");
        auto blockedExecution = cosim::execution(
            startTime, std::make_unique<cosim::fixed_step_algorithm>(stepSize));
        blockedExecution.add_observer(std::make_shared<cosim::file_observer>(
            blockedLogDirectory.path(), blockedConfig));
        bool metadataError = false;
        try {
            blockedExecution.add_slave(std::make_unique<mock_slave>(), "blocked");
            blockedExecution.step();
        } catch (const std::runtime_error& error) {
            metadataError = std::string(error.what()).find(blockedMetadata.string()) != std::string::npos;
        }
        REQUIRE(metadataError);

        const auto exactLogPath =
            cosim::filesystem::current_path() / "whole_value_logs";
        cosim::file_observer_config exactConfig;
        exactConfig.set_timestamped_filenames(false)
            .log_all_simulator_variables("whole value slave");
        auto exactExecution = cosim::execution(
            startTime,
            std::make_unique<cosim::fixed_step_algorithm>(stepSize));
        auto exactObserver = std::make_shared<cosim::file_observer>(
            exactLogPath, exactConfig);
        exactExecution.add_observer(exactObserver);
        exactExecution.add_slave(
            std::make_unique<whole_value_mock_slave>(
                cosim::variable_value(
                    cosim::uint64_values{{7, 8}}, {2})),
            "whole value slave");
        exactExecution.step();
        exactObserver->stop_recording();

        std::ifstream exactLog(exactLogPath / "whole value slave.csv");
        const std::string contents(
            (std::istreambuf_iterator<char>(exactLog)),
            std::istreambuf_iterator<char>());
        REQUIRE(contents.find("\"[7,8]\"") != std::string::npos);
        REQUIRE(contents.find("\"[0,0]\"") != std::string::npos);

        cosim::utility::temp_dir byteLogDirectory;
        cosim::file_observer_config byteConfig;
        byteConfig.set_timestamped_filenames(false)
            .log_all_simulator_variables("numeric bytes");
        auto byteExecution = cosim::execution(
            startTime,
            std::make_unique<cosim::fixed_step_algorithm>(stepSize));
        auto byteObserver = std::make_shared<cosim::file_observer>(
            byteLogDirectory.path(), byteConfig);
        byteExecution.add_observer(byteObserver);
        byteExecution.add_slave(
            std::make_unique<byte_metadata_mock_slave>(), "numeric bytes");
        byteExecution.step();
        byteObserver->stop_recording();

        std::ifstream byteMetadata(byteLogDirectory.path() / "numeric bytes_metadata.yaml");
        const std::string metadata(
            (std::istreambuf_iterator<char>(byteMetadata)),
            std::istreambuf_iterator<char>());
        REQUIRE(metadata.find("start value:  -120") != std::string::npos);
        REQUIRE(metadata.find("start value:  250") != std::string::npos);
        std::ifstream byteLog(byteLogDirectory.path() / "numeric bytes.csv");
        const std::string byteContents(
            (std::istreambuf_iterator<char>(byteLog)),
            std::istreambuf_iterator<char>());
        REQUIRE(byteContents.find("-120,250") != std::string::npos);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
