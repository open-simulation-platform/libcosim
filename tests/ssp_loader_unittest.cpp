#define BOOST_TEST_MODULE ssp_loader.hpp unittests
#include <cse/algorithm/fixed_step_algorithm.hpp>
#include <cse/log/simple.hpp>
#include <cse/observer/last_value_observer.hpp>
#include <cse/ssp/ssp_loader.hpp>

#include <boost/filesystem.hpp>
#include <boost/test/unit_test.hpp>

#include <algorithm>


namespace
{

// Defines the tolerance for the comparison in percentage units.
// See https://www.boost.org/doc/libs/1_65_0/libs/test/doc/html/boost_test/utf_reference/testing_tool_ref/assertion_boost_level_close.html
constexpr double tolerance = 0.0001;

void common_demo_case_tests(const cosim::ssp_configuration& config)
{
    auto execution = cosim::execution(config.start_time, config.algorithm);
    const auto entityMaps = cosim::inject_system_structure(
        execution,
        config.system_structure,
        config.parameter_sets.at(""));

    BOOST_REQUIRE(entityMaps.simulators.size() == 2);
    BOOST_REQUIRE(entityMaps.simulators.count("CraneController") == 1);
    const auto knuckleBoomCrane = entityMaps.simulators.at("KnuckleBoomCrane");

    auto obs = std::make_shared<cosim::last_value_observer>();
    execution.add_observer(obs);
    auto result = execution.simulate_until(cosim::to_time_point(1e-3));
    BOOST_REQUIRE(result.get());

    double realValue = -1.0;
    const auto reference =
        config.system_structure.get_variable_description({"KnuckleBoomCrane", "Spring_Joint.k"}).reference;
    obs->get_real(knuckleBoomCrane, gsl::make_span(&reference, 1), gsl::make_span(&realValue, 1));

    double magicNumberFromSsdFile = 0.005;
    BOOST_CHECK_CLOSE(realValue, magicNumberFromSsdFile, tolerance);

    const auto reference2 =
        config.system_structure.get_variable_description({"KnuckleBoomCrane", "mt0_init"}).reference;
    obs->get_real(knuckleBoomCrane, gsl::make_span(&reference2, 1), gsl::make_span(&realValue, 1));

    magicNumberFromSsdFile = 69.0;
    BOOST_CHECK_CLOSE(realValue, magicNumberFromSsdFile, tolerance);
}

} // namespace

BOOST_AUTO_TEST_CASE(basic_test)
{
    cosim::log::setup_simple_console_logging();
    cosim::log::set_global_output_level(cosim::log::info);

    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_REQUIRE(testDataDir != nullptr);
    boost::filesystem::path sspFile = boost::filesystem::path(testDataDir) / "ssp" / "demo";

    cosim::ssp_loader loader;
    const auto config = loader.load(sspFile);

    common_demo_case_tests(config);
}

BOOST_AUTO_TEST_CASE(no_algorithm_test)
{
    cosim::log::setup_simple_console_logging();
    cosim::log::set_global_output_level(cosim::log::info);

    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_REQUIRE(testDataDir != nullptr);
    boost::filesystem::path sspFile = boost::filesystem::path(testDataDir) / "ssp" / "demo" / "no_algorithm_element";

    cosim::ssp_loader loader;
    auto config = loader.load(sspFile);
    config.algorithm = std::make_unique<cosim::fixed_step_algorithm>(cosim::to_duration(1e-4));

    double startTimeDefinedInSsp = 5.0;
    BOOST_CHECK_CLOSE(cosim::to_double_time_point(config.start_time), startTimeDefinedInSsp, tolerance);

    common_demo_case_tests(config);
}

BOOST_AUTO_TEST_CASE(ssp_archive)
{
    cosim::log::setup_simple_console_logging();
    cosim::log::set_global_output_level(cosim::log::info);

    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_TEST_REQUIRE(testDataDir != nullptr);
    const auto sspFile = boost::filesystem::path(testDataDir) / "ssp" / "demo" / "demo.ssp";

    cosim::ssp_loader loader;
    const auto config = loader.load(sspFile);
    common_demo_case_tests(config);
}

BOOST_AUTO_TEST_CASE(ssp_archive_multiple_ssd)
{
    cosim::log::setup_simple_console_logging();
    cosim::log::set_global_output_level(cosim::log::info);

    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_TEST_REQUIRE(testDataDir != nullptr);
    const auto sspFile = boost::filesystem::path(testDataDir) / "ssp" / "demo" / "demo.ssp";

    cosim::ssp_loader loader;
    loader.set_ssd_file_name("SystemStructure2");
    const auto config = loader.load(sspFile);
    const auto entities = config.system_structure.entities();
    BOOST_REQUIRE(std::distance(entities.begin(), entities.end()) == 1);
}

BOOST_AUTO_TEST_CASE(ssp_linear_transformation_test)
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_TEST_REQUIRE(testDataDir != nullptr);
    const auto sspDir = boost::filesystem::path(testDataDir) / "ssp" / "linear_transformation";

    cosim::ssp_loader loader;
    const auto config = loader.load(sspDir);

    auto exec = cosim::execution(
        config.start_time,
        std::make_unique<cosim::fixed_step_algorithm>(cosim::to_duration(1e-3)));
    const auto entityMaps = cosim::inject_system_structure(
        exec,
        config.system_structure,
        config.parameter_sets.at(""));

    const auto observer = std::make_shared<cosim::last_value_observer>();
    exec.add_observer(observer);

    exec.step();

    double initialValue;
    const auto slave1 = entityMaps.simulators.at("identity1");
    const auto v1Ref =
        config.system_structure.get_variable_description({"identity1", "realOut"}).reference;
    observer->get_real(slave1, gsl::make_span(&v1Ref, 1), gsl::make_span(&initialValue, 1));
    BOOST_REQUIRE_CLOSE(initialValue, 2.0, tolerance);

    double transformedValue;
    const auto slave2 = entityMaps.simulators.at("identity2");
    const auto v2Ref =
        config.system_structure.get_variable_description({"identity2", "realIn"}).reference;
    observer->get_real(slave2, gsl::make_span(&v2Ref, 1), gsl::make_span(&transformedValue, 1));

    double offset = 50;
    double factor = 1.3;
    double target = factor * initialValue + offset;
    BOOST_REQUIRE_CLOSE(transformedValue, target, tolerance);
}

BOOST_AUTO_TEST_CASE(ssp_multiple_parameter_sets_test)
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_TEST_REQUIRE(testDataDir != nullptr);
    const auto sspDir = boost::filesystem::path(testDataDir) / "ssp" / "linear_transformation";

    cosim::ssp_loader loader;
    const auto config = loader.load(sspDir);

    auto exec = cosim::execution(
        config.start_time,
        std::make_unique<cosim::fixed_step_algorithm>(cosim::to_duration(1e-3)));
    const auto entityMaps = cosim::inject_system_structure(
        exec,
        config.system_structure,
        config.parameter_sets.at("initialValues2"));

    auto observer = std::make_shared<cosim::last_value_observer>();
    exec.add_observer(observer);

    exec.step();

    double initialValue;
    const auto slave1 = entityMaps.simulators.at("identity1");
    const auto v1Ref =
        config.system_structure.get_variable_description({"identity1", "realOut"}).reference;
    observer->get_real(slave1, gsl::make_span(&v1Ref, 1), gsl::make_span(&initialValue, 1));
    BOOST_REQUIRE_CLOSE(initialValue, 4.0, tolerance);

}
