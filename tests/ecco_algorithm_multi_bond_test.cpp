#include "mock_slave.hpp"

#include <cosim/algorithm.hpp>
#include <cosim/log/simple.hpp>
#include <cosim/observer/file_observer.hpp>
#include <cosim/observer/last_value_observer.hpp>
#include <cosim/observer/time_series_observer.hpp>
#include <cosim/osp_config_parser.hpp>
#include <cosim/time.hpp>

#include <exception>
#include <memory>
#include <stdexcept>


// A helper macro to test various assertions
#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

int main()
{
    try {
        cosim::log::setup_simple_console_logging();
        cosim::log::set_global_output_level(cosim::log::info);

        constexpr cosim::time_point startTime = cosim::to_time_point(0.0);
        ;
        constexpr cosim::time_point midTime = cosim::to_time_point(4.0);

        auto ecco_params = cosim::ecco_parameters{
            0.8,
            cosim::to_duration(1e-4),
            cosim::to_duration(1e-4),
            cosim::to_duration(0.01),
            0.2,
            1.5,
            1e-4,
            1e-4,
            0.2,
            0.15};

        auto ecco_algo = std::make_shared<cosim::ecco_algorithm>(ecco_params);
        const auto testDataDir = std::getenv("TEST_DATA_DIR");
        const auto configPath = cosim::filesystem::path(testDataDir) / "ecco" / "quarter_truck" / "OspSystemStructure.xml";
        const auto logXmlPath = cosim::filesystem::path(testDataDir) / "ecco" / "quarter_truck" / "LogConfig.xml";

        auto resolver = cosim::default_model_uri_resolver();
        const auto config = cosim::load_osp_config(configPath, *resolver);
        auto execution = cosim::execution(
            config.start_time,
            ecco_algo);

        const auto entityMaps = cosim::inject_system_structure(execution, config.system_structure, config.initial_values);
        REQUIRE(entityMaps.simulators.size() == 2);


        // Default should not be real time
        const auto realTimeConfig = execution.get_real_time_config();
        REQUIRE(!realTimeConfig->real_time_simulation);

        // auto chassisIndex = entityMaps.simulators.at("chassis");
        // auto wheelIndex = entityMaps.simulators.at("wheel");
        // auto groundIndex = entityMaps.simulators.at("ground");

        auto chassisIndex = entityMaps.simulators.at("chassis");
        auto wheelIndex = entityMaps.simulators.at("wheel");

        // auto gravity = cosim::variable_id{chassisIndex, cosim::variable_type::real, 2};
        auto chassisForce = cosim::variable_id{chassisIndex, cosim::variable_type::real, 19};
        auto chassisVel = cosim::variable_id{chassisIndex, cosim::variable_type::real, 22};
        auto wheelCForce = cosim::variable_id{wheelIndex, cosim::variable_type::real, 26};
        auto wheelCVel = cosim::variable_id{wheelIndex, cosim::variable_type::real, 24};
        // auto wheelGForce = cosim::variable_id{wheelIndex, cosim::variable_type::real, 17};
        // auto wheelGVel = cosim::variable_id{wheelIndex, cosim::variable_type::real, 18};
        // auto groundForce = cosim::variable_id{groundIndex, cosim::variable_type::real, 11};
        // auto groundVel = cosim::variable_id{groundIndex, cosim::variable_type::real, 12};

        // a = chassis b = wheel
        // a = wheel   b = ground
        // u_a = y_b --- u_b = y_a
        //                          u_a           y_a           u_b         y_b
        ecco_algo->add_power_bond(chassisVel, chassisForce, wheelCForce, wheelCVel); // chassis -> wheel (chassis port)
        // ecco_algo->add_power_bond(wheelGVel, wheelGForce, groundForce, groundVel); // wheel -> ground (ground port)

        auto file_obs = std::make_unique<cosim::file_observer>("./logDir", logXmlPath);
        execution.add_observer(std::move(file_obs));

        // auto simResult = execution.simulate_until(midTime);
        // REQUIRE(simResult);


        // Add an observer that watches the last slave
        auto t_observer = std::make_shared<cosim::time_series_observer>(50000);
        execution.add_observer(t_observer);
        t_observer->start_observing(chassisVel);
        t_observer->start_observing(wheelCVel);
        // t_observer->start_observing(wheelGVel);
        // t_observer->start_observing(groundVel);
        t_observer->start_observing(chassisForce);
        t_observer->start_observing(wheelCForce);
        // t_observer->start_observing(wheelGForce);
        // t_observer->start_observing(groundForce);

        auto csv_observer = std::make_shared<cosim::file_observer>(".");
        execution.add_observer(csv_observer);

        // Run simulation
        auto simResult = execution.simulate_until(midTime);
        REQUIRE(simResult);

        cosim::step_number stepNums[2]{};
        t_observer->get_step_numbers(chassisVel.simulator, startTime, midTime, gsl::make_span(stepNums, 2));
        const auto numSamples = stepNums[1] - stepNums[0];
        // const int numSamples = 11;
        std::vector<double> chassisVels(numSamples);
        std::vector<double> wheelCVels(numSamples);
        std::vector<double> wheelGVels(numSamples);
        std::vector<double> groundVels(numSamples);
        std::vector<cosim::step_number> steps(numSamples);
        std::vector<cosim::time_point> timeValues(numSamples);

        t_observer->get_real_samples(
            chassisVel.simulator,
            chassisVel.reference,
            0,
            gsl::make_span(chassisVels),
            gsl::make_span(steps),
            gsl::make_span(timeValues));
        t_observer->get_real_samples(
            wheelCVel.simulator,
            wheelCVel.reference,
            0,
            gsl::make_span(wheelCVels),
            gsl::make_span(steps),
            gsl::make_span(timeValues));
        // t_observer->get_real_samples(
        //     wheelGVel.simulator,
        //     wheelGVel.reference,
        //     0,
        //     gsl::make_span(wheelGVels),
        //     gsl::make_span(steps),
        //     gsl::make_span(timeValues));
        // t_observer->get_real_samples(
        //     groundVel.simulator,
        //     groundVel.reference,
        //     0,
        //     gsl::make_span(groundVels),
        //     gsl::make_span(steps),
        //     gsl::make_span(timeValues));


        std::cout << "time,step #,stepsize,chassisVel,wheelCVel,wheelGVel,groundVel" << std::endl;
        for (int i = 1; i < numSamples; ++i) {
            std::cout << cosim::to_double_time_point(timeValues[i])
                      << "," << steps[i]
                      << "," << cosim::to_double_duration(timeValues[i] - timeValues[i - 1], timeValues[i - 1])
                      << "," << chassisVels[i]
                      << "," << wheelCVels[i]
                      << "," << wheelGVels[i]
                      << "," << groundVels[i]
                      << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
