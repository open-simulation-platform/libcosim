#include <cmath>
#include <exception>
#include <memory>
#include <stdexcept>

#include <cse/algorithm.hpp>
#include <cse/async_slave.hpp>
#include <cse/execution.hpp>
#include <cse/fmi/fmu.hpp>
#include <cse/fmi/importer.hpp>
#include <cse/log.hpp>
#include <cse/ssp_parser.hpp>

#include <boost/fiber/future.hpp>
#include <boost/filesystem.hpp>


#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

struct slave_info
{
    cse::simulator_index index;
    std::map<std::string, cse::variable_description> variables;
};

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


        std::map<std::string, slave_info> slaves;

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
            slaves[component.name].index = execution.add_slave(cse::make_pseudo_async(slave), component.name);
            for (const auto& v : fmu->model_description()->variables)
            {
                slaves[component.name].variables[v.name] = v;
            }
        }

        for (const auto& connection : parser.get_connections()) {
            cse::variable_id output = {slaves[connection.startElement].index,
                                       slaves[connection.startElement].variables[connection.startConnector].type,
                                       slaves[connection.startElement].variables[connection.startConnector].index};

            cse::variable_id input  = {slaves[connection.endElement].index,
                                       slaves[connection.endElement].variables[connection.endConnector].type,
                                       slaves[connection.endElement].variables[connection.endConnector].index};

            execution.connect_variables(output, input);
        }

        auto result = execution.simulate_until(endTime);
        REQUIRE(result.get());

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what();
        return 1;
    }

    return 0;
}
