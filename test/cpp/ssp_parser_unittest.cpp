#define BOOST_TEST_MODULE ssp_parser.hpp unittests
#include <cse/algorithm/fixed_step_algorithm.hpp>
#include <cse/connection/linear_transformation_connection.hpp>
#include <cse/log/simple.hpp>
#include <cse/observer/last_value_observer.hpp>
#include <cse/ssp_parser.hpp>

#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>

namespace
{

// Defines the tolerance for the comparison in percentage units.
// See https://www.boost.org/doc/libs/1_65_0/libs/test/doc/html/boost_test/utf_reference/testing_tool_ref/assertion_boost_level_close.html
constexpr double tolerance = 0.0001;

void common_demo_case_tests(cse::execution& execution, cse::simulator_map simulatorMap)
{
    BOOST_REQUIRE(simulatorMap.size() == 2);
    auto craneController = simulatorMap.at("CraneController");
    auto knuckleBoomCrane = simulatorMap.at("KnuckleBoomCrane");

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

} // namespace

BOOST_AUTO_TEST_CASE(basic_test)
{
    cse::log::setup_simple_console_logging();
    cse::log::set_global_output_level(cse::log::info);

    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_REQUIRE(testDataDir != nullptr);
    boost::filesystem::path xmlPath = boost::filesystem::path(testDataDir) / "ssp" / "demo";

    auto resolver = cse::default_model_uri_resolver();
    auto [execution, simulatorMap] = cse::load_ssp(*resolver, xmlPath);

    auto craneController = simulatorMap.at("CraneController");
    auto knuckleBoomCrane = simulatorMap.at("KnuckleBoomCrane");

    BOOST_REQUIRE(craneController.source == "CraneController.fmu");
    BOOST_REQUIRE(knuckleBoomCrane.source == "KnuckleBoomCrane.fmu");

    common_demo_case_tests(execution, simulatorMap);
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
    auto [execution, simulatorMap] = cse::load_ssp(*resolver, xmlPath, std::move(algorithm));

    double startTimeDefinedInSsp = 5.0;
    BOOST_CHECK_CLOSE(cse::to_double_time_point(execution.current_time()), startTimeDefinedInSsp, tolerance);

    common_demo_case_tests(execution, simulatorMap);
}

BOOST_AUTO_TEST_CASE(ssp_archive)
{
    cse::log::setup_simple_console_logging();
    cse::log::set_global_output_level(cse::log::info);

    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_TEST_REQUIRE(testDataDir != nullptr);
    const auto sspFile = boost::filesystem::path(testDataDir) / "ssp" / "demo" / "demo.ssp";

    auto resolver = cse::default_model_uri_resolver();
    auto [execution, simulatorMap] = cse::load_ssp(*resolver, sspFile);
    common_demo_case_tests(execution, simulatorMap);
}

BOOST_AUTO_TEST_CASE(ssp_linear_transformation_test)
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_TEST_REQUIRE(testDataDir != nullptr);
    const auto sspDir = boost::filesystem::path(testDataDir) / "ssp" / "linear_transformation";

    auto resolver = cse::default_model_uri_resolver();
    auto algorithm = std::make_shared<cse::fixed_step_algorithm>(cse::to_duration(1e-3));
    auto [exec, simulatorMap] = cse::load_ssp(*resolver, sspDir, algorithm);

    auto observer = std::make_shared<cse::last_value_observer>();
    exec.add_observer(observer);

    exec.step();

    double initialValue;
    auto slave1 = simulatorMap.at("identity1");
    cse::value_reference v1Ref = cse::find_variable(slave1.description, "realOut").reference;
    observer->get_real(slave1.index, gsl::make_span(&v1Ref, 1), gsl::make_span(&initialValue, 1));
    BOOST_REQUIRE_CLOSE(initialValue, 2.0, tolerance);

    double transformedValue;
    auto slave2 = simulatorMap.at("identity2");
    cse::value_reference v2Ref = cse::find_variable(slave2.description, "realIn").reference;
    observer->get_real(slave2.index, gsl::make_span(&v2Ref, 1), gsl::make_span(&transformedValue, 1));

    double offset = 50;
    double factor = 1.3;
    double target = factor * initialValue + offset;
    BOOST_REQUIRE_CLOSE(transformedValue, target, tolerance);
}
