#include <cse/fmuproxy/fmuproxy_uri_sub_resolver.hpp>
#include <cse/log/simple.hpp>
#include <cse/ssp_loader.hpp>

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

        cse::ssp_loader loader;
        loader.set_algorithm(std::make_unique<cse::fixed_step_algorithm>(cse::to_duration(1e-4)));
        auto simulation = loader.load(xmlPath);
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
