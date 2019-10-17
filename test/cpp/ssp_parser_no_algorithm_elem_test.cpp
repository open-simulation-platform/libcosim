// TODO: Turn this into a unit test.
#include <cse/algorithm.hpp>
#include <cse/log/simple.hpp>
#include <cse/observer/last_value_observer.hpp>
#include <cse/ssp_parser.hpp>

#include <boost/filesystem.hpp>
#include <boost/range/distance.hpp>

#include <cstdlib>
#include <exception>

#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

int main()
{
    try {
        cse::log::setup_simple_console_logging();
        cse::log::set_global_output_level(cse::log::info);

        const auto testDataDir = std::getenv("TEST_DATA_DIR");
        REQUIRE(testDataDir);
        boost::filesystem::path xmlPath = boost::filesystem::path(testDataDir) / "ssp" / "demo" / "no_algorithm_element" / "SystemStructure.ssd";

        auto resolver = cse::default_model_uri_resolver();
        auto [system, params, defaults] = cse::load_ssp_v2(*resolver, xmlPath);
        REQUIRE(distance(system.simulators()) == 2);
        REQUIRE(distance(system.scalar_connections()) == 9);
        REQUIRE(params.size() == 2);
        REQUIRE(params.at({"KnuckleBoomCrane", "Spring_Joint.k"}) == cse::scalar_value(0.005));
        REQUIRE(params.at({"KnuckleBoomCrane", "mt0_init"}) == cse::scalar_value(69.0));
        REQUIRE(defaults.start_time == cse::to_time_point(5.0));
        REQUIRE(!defaults.stop_time);
        REQUIRE(!defaults.step_size);
        REQUIRE(!defaults.algorithm);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what();
        return 1;
    }
    return 0;
}
