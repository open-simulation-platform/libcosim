#include <cosim/algorithm/fixed_step_algorithm.hpp>
#include <cosim/osp_config_parser.hpp>
#include <cosim/observer/file_observer.hpp>
#include <cosim/exception.hpp>
#include <cosim/execution.hpp>
#include <cosim/function/linear_transformation.hpp>
#include <cosim/observer/last_value_observer.hpp>
#include <cosim/system_structure.hpp>
#include <algorithm>
#include <iostream>

#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

int main()
{
    try {
        const auto testDataDir = std::getenv("TEST_DATA_DIR");
        REQUIRE(!!testDataDir);

        cosim::filesystem::path configPath = testDataDir;

        auto resolver = cosim::default_model_uri_resolver();
        const auto config = cosim::load_osp_config(configPath / "msmi" / "OspSystemStructure_StateInitExample.xml", *resolver);

        auto execution = cosim::execution(
            config.start_time,
            std::make_shared<cosim::fixed_step_algorithm>(config.step_size));

        const auto entityMaps = cosim::inject_system_structure(
            execution, config.system_structure, config.initial_values);
        auto lvObserver = std::make_shared<cosim::last_value_observer>();

        execution.add_observer(lvObserver);
        execution.simulate_until(cosim::to_time_point(0.1));

        auto sim = entityMaps.simulators.at("example"); 
        const cosim::value_reference inRef = 2;
        const cosim::value_reference outRef = 3;

        double initialValue = 0.0;
        double outputValue = 0.0;
        lvObserver->get_real(sim, gsl::make_span(&inRef, 1), gsl::make_span(&initialValue, 1));
        lvObserver->get_real(sim, gsl::make_span(&outRef, 1), gsl::make_span(&outputValue, 1));

        REQUIRE(initialValue == 1.0);
        REQUIRE(std::fabs(outputValue - 10.1) < 1.0e-9);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}