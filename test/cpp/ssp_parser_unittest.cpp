#define BOOST_TEST_MODULE ssp_parser.hpp unittests
#include <cse/algorithm/fixed_step_algorithm.hpp>
#include <cse/log/simple.hpp>
#include <cse/observer/last_value_observer.hpp>
#include <cse/ssp_parser.hpp>

#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>

namespace
{

constexpr double tolerance = 1e-9;

}

BOOST_AUTO_TEST_CASE(basic_test)
{
    cse::log::setup_simple_console_logging();
    cse::log::set_global_output_level(cse::log::info);

    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_REQUIRE(testDataDir != nullptr);
    boost::filesystem::path xmlPath = boost::filesystem::path(testDataDir) / "ssp" / "demo";

    auto resolver = cse::default_model_uri_resolver();
    auto [execution, simulator_map] = cse::load_ssp(*resolver, xmlPath);

    BOOST_REQUIRE(simulator_map.size() == 2);

    auto craneController = simulator_map.at("CraneController");
    auto knuckleBoomCrane = simulator_map.at("KnuckleBoomCrane");

    BOOST_REQUIRE(craneController.source == "CraneController.fmu");
    BOOST_REQUIRE(knuckleBoomCrane.source == "KnuckleBoomCrane.fmu");

    auto obs = std::make_shared<cse::last_value_observer>();
    execution.add_observer(obs);
    auto result = execution.simulate_until(cse::to_time_point(1e-3));
    BOOST_REQUIRE(result.get());

    double realValue = -1.0;
    cse::value_reference reference = cse::find_variable(knuckleBoomCrane.description, "Spring_Joint.k").reference;
    obs->get_real(knuckleBoomCrane.index, gsl::make_span(&reference, 1), gsl::make_span(&realValue, 1));

    double magicNumberFromSsdFile = 0.005;
    BOOST_CHECK_CLOSE(realValue, magicNumberFromSsdFile, tolerance);

    cse::value_reference reference2 = cse::find_variable(knuckleBoomCrane.description, "mt0_init").reference;
    obs->get_real(knuckleBoomCrane.index, gsl::make_span(&reference2, 1), gsl::make_span(&realValue, 1));

    magicNumberFromSsdFile = 69.0;
    BOOST_CHECK_CLOSE(realValue, magicNumberFromSsdFile, tolerance);
}

BOOST_AUTO_TEST_CASE(no_algorithm_test)
{
    cse::log::setup_simple_console_logging();
    cse::log::set_global_output_level(cse::log::info);

    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_REQUIRE(testDataDir != nullptr);
    boost::filesystem::path xmlPath = boost::filesystem::path(testDataDir) / "ssp" / "demo" / "no_algorithm_element";

    auto resolver = cse::default_model_uri_resolver();
    auto algorithm = std::make_unique<cse::fixed_step_algorithm>(cse::to_duration(1e-4));
    auto [execution, simulator_map] = cse::load_ssp(*resolver, xmlPath, std::move(algorithm));

    BOOST_REQUIRE(simulator_map.size() == 2);

    auto knuckleBoomCrane = simulator_map.at("KnuckleBoomCrane");

    double startTimeDefinedInSsp = 5.0;
    BOOST_CHECK_CLOSE(cse::to_double_time_point(execution.current_time()), startTimeDefinedInSsp, tolerance);

    auto obs = std::make_shared<cse::last_value_observer>();
    execution.add_observer(obs);
    auto result = execution.simulate_until(cse::to_time_point(1e-3));
    BOOST_REQUIRE(result.get());

    double realValue = -1.0;
    cse::value_reference reference = cse::find_variable(knuckleBoomCrane.description, "Spring_Joint.k").reference;
    obs->get_real(knuckleBoomCrane.index, gsl::make_span(&reference, 1), gsl::make_span(&realValue, 1));

    double magicNumberFromSsdFile = 0.005;
    BOOST_CHECK_CLOSE(realValue, magicNumberFromSsdFile, tolerance);

    cse::value_reference reference2 = cse::find_variable(knuckleBoomCrane.description, "mt0_init").reference;
    obs->get_real(knuckleBoomCrane.index, gsl::make_span(&reference2, 1), gsl::make_span(&realValue, 1));

    magicNumberFromSsdFile = 69.0;
    BOOST_CHECK_CLOSE(realValue, magicNumberFromSsdFile, tolerance);
}
