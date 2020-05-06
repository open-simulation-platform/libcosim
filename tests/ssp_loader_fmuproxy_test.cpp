#include <cosim/log/simple.hpp>
#include <cosim/ssp/ssp_loader.hpp>

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
        boost::filesystem::path sspFile = boost::filesystem::path(testDataDir) / "ssp" / "demo" / "fmuproxy";

        cse::ssp_loader loader;
        const auto config = loader.load(sspFile);
        auto exec = cse::execution(config.start_time, config.algorithm);
        const auto entityMaps = cse::inject_system_structure(
            exec,
            config.system_structure,
            config.parameter_sets.at(""));
        REQUIRE(entityMaps.simulators.size() == 2);

        auto result = exec.simulate_until(cse::to_time_point(1e-3));
        REQUIRE(result.get());
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what();
        return 1;
    }
    return 0;
}
