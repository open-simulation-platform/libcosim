#include <cosim/algorithm.hpp>
#include <cosim/log/simple.hpp>
#include <cosim/observer/last_value_observer.hpp>
#include <cosim/observer/time_series_observer.hpp>
#include <cosim/time.hpp>
#include <cosim/fs_portability.hpp>
#include <cosim/orchestration.hpp>
#include <cosim/osp_config_parser.hpp>

#include <exception>
#include <memory>
#include <stdexcept>
#include <cstdlib>

// A helper macro to test various assertions
#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

int main()
{
    try {
        const auto testDataDir = std::getenv("TEST_DATA_DIR");
        REQUIRE(testDataDir);
        cosim::log::setup_simple_console_logging();
        cosim::log::set_global_output_level(cosim::log::debug);

        auto resolver = cosim::default_model_uri_resolver();
        auto configPath = cosim::filesystem::path(testDataDir) / "ecco" / "quarter_truck";
        std::cout << "Config path is " << configPath << std::endl;

        const auto config = cosim::load_osp_config(configPath, *resolver);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
