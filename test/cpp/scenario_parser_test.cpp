#include "mock_slave.hpp"

#include <cse/scenario_parser.hpp>
#include <cse/log.hpp>

#include <exception>
#include <memory>
#include <stdexcept>
#include <vector>

#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

int main()
{
    try {
        cse::log::set_global_output_level(cse::log::level::trace);

        const auto testDataDir = std::getenv("TEST_DATA_DIR");
        REQUIRE(testDataDir);
        boost::filesystem::path jsonPath = boost::filesystem::path(testDataDir) / "scenarios" / "scenario1.json";
        cse::scenario::scenario testy = cse::parse_scenario(jsonPath);


    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
