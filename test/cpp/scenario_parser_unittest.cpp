#define BOOST_TEST_MODULE scenario_parser.hpp unittests

#include "mock_slave.hpp"

#include <cse/algorithm.hpp>
#include <cse/log/simple.hpp>
#include <cse/manipulator.hpp>
#include <cse/observer/time_series_observer.hpp>

#include <boost/test/unit_test.hpp>

#include <exception>
#include <memory>

namespace
{

// Defines the tolerance for the comparison in percentage units.
// See https://www.boost.org/doc/libs/1_65_0/libs/test/doc/html/boost_test/utf_reference/testing_tool_ref/assertion_boost_level_close.html
constexpr double tolerance = 0.0001;

constexpr cse::time_point startTime = cse::to_time_point(0.0);
constexpr cse::time_point endTime = cse::to_time_point(1.1);
constexpr cse::duration stepSize = cse::to_duration(0.1);

void test(const boost::filesystem::path& scenarioFile)
{

    cse::log::setup_simple_console_logging();
    cse::log::set_global_output_level(cse::log::trace);

    auto execution = cse::execution(startTime, std::make_unique<cse::fixed_step_algorithm>(stepSize));

    auto observer = std::make_shared<cse::time_series_observer>();
    execution.add_observer(observer);
    auto scenarioManager = std::make_shared<cse::scenario_manager>();
    execution.add_manipulator(scenarioManager);

    auto simIndex = execution.add_slave(
        cse::make_pseudo_async(std::make_unique<mock_slave>(
            [](double x) { return x + 1.234; },
            [](int y) { return y + 2; })),
        "slave uno");

    observer->start_observing(cse::variable_id{simIndex, cse::variable_type::real, 0});
    observer->start_observing(cse::variable_id{simIndex, cse::variable_type::real, 1});
    observer->start_observing(cse::variable_id{simIndex, cse::variable_type::integer, 0});
    observer->start_observing(cse::variable_id{simIndex, cse::variable_type::integer, 1});

    scenarioManager->load_scenario(scenarioFile, startTime);

    auto simResult = execution.simulate_until(endTime);
    BOOST_REQUIRE(simResult.get());

    const int numSamples = 11;
    double realInputValues[numSamples];
    double realOutputValues[numSamples];
    int intInputValues[numSamples];
    int intOutputValues[numSamples];
    cse::step_number steps[numSamples];
    cse::time_point times[numSamples];

    size_t samplesRead = observer->get_real_samples(simIndex, 1, 1, gsl::make_span(realInputValues, numSamples), gsl::make_span(steps, numSamples), gsl::make_span(times, numSamples));
    BOOST_CHECK(samplesRead == 11);
    samplesRead = observer->get_real_samples(simIndex, 0, 1, gsl::make_span(realOutputValues, numSamples), gsl::make_span(steps, numSamples), gsl::make_span(times, numSamples));
    BOOST_CHECK(samplesRead == 11);
    samplesRead = observer->get_integer_samples(simIndex, 1, 1, gsl::make_span(intInputValues, numSamples), gsl::make_span(steps, numSamples), gsl::make_span(times, numSamples));
    BOOST_CHECK(samplesRead == 11);
    samplesRead = observer->get_integer_samples(simIndex, 0, 1, gsl::make_span(intOutputValues, numSamples), gsl::make_span(steps, numSamples), gsl::make_span(times, numSamples));
    BOOST_CHECK(samplesRead == 11);

    double expectedRealInputs[] = {0.0, 0.0, 0.0, 0.0, 0.0, 2.001, 2.001, 2.001, 2.001, 2.001, 1.0};
    double expectedRealOutputs[] = {1.234, 1.234, -1.0, 1.234, 1.234, 3.235, 3.235, 3.235, 3.235, 3.235, 2.234};
    int expectedIntInputs[] = {0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 1};
    int expectedIntOutputs[] = {2, 2, 2, 2, 2, 2, 2, 4, 5, 5, 3};

    for (size_t i = 0; i < samplesRead; i++) {
        BOOST_CHECK_CLOSE(realInputValues[i], expectedRealInputs[i], tolerance);
        BOOST_CHECK_CLOSE(realOutputValues[i], expectedRealOutputs[i], tolerance);
        BOOST_CHECK(intInputValues[i] == expectedIntInputs[i]);
        BOOST_CHECK(intOutputValues[i] == expectedIntOutputs[i]);
    }
}

} // namespace

BOOST_AUTO_TEST_CASE(json_test)
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_REQUIRE(testDataDir != nullptr);

    test(boost::filesystem::path(testDataDir) / "scenarios" / "scenario1.json");
}

BOOST_AUTO_TEST_CASE(yaml_test)
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_REQUIRE(testDataDir != nullptr);

    test(boost::filesystem::path(testDataDir) / "scenarios" / "scenario1.yml");
}
