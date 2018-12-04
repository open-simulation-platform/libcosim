#include <cmath>
#include <exception>
#include <memory>
#include <stdexcept>

#include <cse/algorithm.hpp>
#include <cse/async_slave.hpp>
#include <cse/execution.hpp>
#include <cse/ssp_parser.hpp>
#include <cse/fmi/importer.hpp>
#include <cse/fmi/fmu.hpp>
#include <cse/log.hpp>

#include <boost/fiber/future.hpp>
#include <boost/filesystem.hpp>


#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

int main()
{
    try {

    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    REQUIRE(testDataDir);

    boost::filesystem::path xmlPath = boost::filesystem::path(testDataDir) / "ssp" / "demo" / "SystemStructure.ssd";
    std::cout << "Path: " << xmlPath.string() << std::endl;

    const auto parser = cse::ssp_parser(xmlPath);

    auto simInfo = parser.get_simulation_information();
    auto defExp = parser.get_default_experiment();

    const cse::time_point startTime = cse::to_time_point(defExp.startTime);
    const cse::time_point endTime = cse::to_time_point(defExp.stopTime);
    const cse::duration stepSize = cse::to_duration(simInfo.stepSize);

    auto elems = parser.get_elements();
    for (const auto& comp : elems) {
        std::cout << "Name:" << comp.name << std::endl;
        std::cout << "Source: " << comp.source << std::endl;
    }


    cse::log::set_global_output_level(cse::log::level::info);

    // Set up execution
    auto execution = cse::execution(
            startTime,
            std::make_unique<cse::fixed_step_algorithm>(stepSize));

    auto importer = cse::fmi::importer::create();
    for (const auto& component : elems) {
        auto fmu = importer->import(
                boost::filesystem::path(testDataDir) / "ssp" / "demo" / component.source);
        auto slave = fmu->instantiate_slave(component.name);
        execution.add_slave(cse::make_pseudo_async(slave), component.name);
    }

    auto result = execution.simulate_until(endTime);
    REQUIRE(result.get());

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what();
        return 1;
    }

    return 0;
}