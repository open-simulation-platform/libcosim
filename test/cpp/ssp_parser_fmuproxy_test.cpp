#include <cse/algorithm.hpp>
#include <cse/fmuproxy/fmuproxy_uri_sub_resolver.hpp>
#include <cse/log/simple.hpp>
#include <cse/ssp_parser.hpp>

#include <boost/filesystem.hpp>

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
        boost::filesystem::path xmlPath = boost::filesystem::path(testDataDir) / "ssp" / "demo" / "fmuproxy";

        auto resolver = cse::default_model_uri_resolver();
        resolver->add_sub_resolver(std::make_shared<cse::fmuproxy::fmuproxy_uri_sub_resolver>());
        auto simulation = cse::load_ssp(*resolver, xmlPath, std::make_unique<cse::fixed_step_algorithm>(cse::to_duration(1e-4)));
        auto& execution = simulation.first;

        auto& simulator_map = simulation.second;
        REQUIRE(simulator_map.size() == 2);

        auto result = execution.simulate_until(cse::to_time_point(1e-3));
        REQUIRE(result.get());
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what();
        return 1;
    }
    return 0;
}
