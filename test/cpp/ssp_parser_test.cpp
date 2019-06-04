#include <cse/log.hpp>
#include <cse/ssp_parser.hpp>

#include <boost/filesystem.hpp>

#include <cstdlib>
#include <exception>


#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

int main()
{
    try {
        cse::log::set_global_output_level(cse::log::level::info);

        const auto testDataDir = std::getenv("TEST_DATA_DIR");
        REQUIRE(testDataDir);
        boost::filesystem::path xmlPath = boost::filesystem::path(testDataDir) / "ssp" / "demo";

        auto resolver = cse::default_model_uri_resolver();
        auto simulation = cse::load_ssp(*resolver, xmlPath, cse::to_time_point(0.0));
        auto& execution = simulation.first;

        auto& simulator_map = simulation.second;
        REQUIRE(simulator_map.size() == 2);
        REQUIRE(simulator_map.at("CraneController").source == "CraneController.fmu");
        REQUIRE(simulator_map.at("KnuckleBoomCrane").source == "KnuckleBoomCrane.fmu");

        auto result = execution.simulate_until(cse::to_time_point(1e-3));
        REQUIRE(result.get());
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what();
        return 1;
    }
    return 0;
}
