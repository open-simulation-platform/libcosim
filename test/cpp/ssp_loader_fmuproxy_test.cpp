#include <cse/log/simple.hpp>
#include <cse/ssp/ssp_loader.hpp>

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
        auto [exec, simulatorMap] = loader.load(sspFile);
        REQUIRE(simulatorMap.size() == 2);

        auto result = exec.simulate_until(cse::to_time_point(1e-3));
        REQUIRE(result.get());
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what();
        return 1;
    }
    return 0;
}
