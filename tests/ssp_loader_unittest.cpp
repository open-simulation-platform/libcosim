#define BOOST_TEST_MODULE ssp_loader.hpp unittests
#include <cosim/algorithm/fixed_step_algorithm.hpp>
#include <cosim/exception.hpp>
#include <cosim/fs_portability.hpp>
#include <cosim/log/simple.hpp>
#include <cosim/observer/last_value_observer.hpp>
#include <cosim/ssp/ssp_loader.hpp>

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
    BOOST_REQUIRE(result);

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
    cosim::filesystem::path sspFile = cosim::filesystem::path(testDataDir) / "ssp" / "demo";

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
    cosim::filesystem::path sspFile = cosim::filesystem::path(testDataDir) / "ssp" / "demo" / "no_algorithm_element";

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
    const auto sspFile = cosim::filesystem::path(testDataDir) / "ssp" / "demo" / "demo.ssp";

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
    const auto sspFile = cosim::filesystem::path(testDataDir) / "ssp" / "demo" / "demo.ssp";

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
    const auto sspDir = cosim::filesystem::path(testDataDir) / "ssp" / "linear_transformation";

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
    const auto sspDir = cosim::filesystem::path(testDataDir) / "ssp" / "linear_transformation";

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

BOOST_AUTO_TEST_CASE(missing_ssd_is_bad_file)
{
    const auto dir = cosim::filesystem::temp_directory_path() / "libcosim_ssp_loader_unittest_empty";
    cosim::filesystem::create_directories(dir);

    cosim::ssp_loader loader;
    BOOST_CHECK_EXCEPTION(
        loader.load(dir),
        cosim::error,
        [](const cosim::error& e) { return e.code() == cosim::errc::bad_file; });

    cosim::filesystem::remove_all(dir);
}

namespace
{
cosim::value_reference find_reference(
    const cosim::execution& exe,
    cosim::simulator_index sim,
    const std::string& name)
{
    for (const auto& v : exe.get_model_description(sim).variables) {
        if (v.name == name) return v.reference;
    }
    throw std::runtime_error("No variable named " + name);
}
} // namespace

BOOST_AUTO_TEST_CASE(load_execution_from_ssp_test)
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_TEST_REQUIRE(testDataDir != nullptr);
    const auto sspDir = cosim::filesystem::path(testDataDir) / "ssp" / "demo";

    // Uses the algorithm from the SSP's default experiment
    auto configured = cosim::load_execution_from_ssp(sspDir);
    BOOST_REQUIRE(configured.execution);
    BOOST_REQUIRE(configured.entity_maps.simulators.size() == 2);
    BOOST_CHECK(!configured.end_time);

    auto& exe = *configured.execution;
    const auto crane = configured.entity_maps.simulators.at("KnuckleBoomCrane");
    auto obs = std::make_shared<cosim::last_value_observer>();
    exe.add_observer(obs);
    BOOST_REQUIRE(exe.simulate_until(exe.current_time() + cosim::to_duration(1e-3)));

    // Parameter values from the SSD's default parameter set are applied
    const auto values = obs->get_real(crane, {find_reference(exe, crane, "Spring_Joint.k")});
    BOOST_CHECK_CLOSE(values.at(0), 0.005, tolerance);
}

BOOST_AUTO_TEST_CASE(load_execution_from_ssp_algorithm_test)
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_TEST_REQUIRE(testDataDir != nullptr);
    const auto sspDir = cosim::filesystem::path(testDataDir) / "ssp" / "demo" / "no_algorithm_element";

    // No algorithm in the SSP and none given
    BOOST_CHECK_EXCEPTION(
        cosim::load_execution_from_ssp(sspDir),
        cosim::error,
        [](const cosim::error& e) { return e.code() == cosim::errc::bad_file; });

    // An explicitly given algorithm is used
    cosim::execution_load_options options;
    options.algorithm = std::make_shared<cosim::fixed_step_algorithm>(cosim::to_duration(1e-4));
    auto configured = cosim::load_execution_from_ssp(sspDir, options);
    BOOST_CHECK_CLOSE(cosim::to_double_time_point(configured.execution->current_time()), 5.0, tolerance);
    BOOST_REQUIRE(configured.execution->simulate_until(cosim::to_time_point(5.001)));

    // Start time override and a custom model resolver
    options.algorithm = std::make_shared<cosim::fixed_step_algorithm>(cosim::to_duration(1e-4));
    options.start_time = cosim::to_time_point(2.0);
    options.model_resolver = cosim::default_model_uri_resolver();
    auto restarted = cosim::load_execution_from_ssp(sspDir, options);
    BOOST_CHECK_CLOSE(cosim::to_double_time_point(restarted.execution->current_time()), 2.0, tolerance);
    BOOST_REQUIRE(restarted.execution->simulate_until(cosim::to_time_point(2.001)));

    // Unknown parameter set
    options.algorithm = std::make_shared<cosim::fixed_step_algorithm>(cosim::to_duration(1e-4));
    options.parameter_set = "no_such_parameter_set";
    BOOST_CHECK_EXCEPTION(
        cosim::load_execution_from_ssp(sspDir, options),
        cosim::error,
        [](const cosim::error& e) { return e.code() == cosim::errc::bad_file; });
}
