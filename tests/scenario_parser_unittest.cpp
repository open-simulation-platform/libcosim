
#include "mock_slave.hpp"

#include <cosim/algorithm.hpp>
#include <cosim/log/simple.hpp>
#include <cosim/manipulator.hpp>
#include <cosim/observer/time_series_observer.hpp>

#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

#include <exception>
#include <memory>

namespace
{

// Defines the tolerance for the comparison in percentage units.
// See https://www.boost.org/doc/libs/1_65_0/libs/test/doc/html/CHECK/utf_reference/testing_tool_ref/assertion_boost_level_close.html
constexpr double tolerance = 0.0001;

constexpr cosim::time_point startTime = cosim::to_time_point(0.0);
constexpr cosim::time_point endTime = cosim::to_time_point(1.1);
constexpr cosim::duration stepSize = cosim::to_duration(0.1);

void test(const cosim::filesystem::path& scenarioFile)
{

    cosim::log::setup_simple_console_logging();
    cosim::log::set_global_output_level(cosim::log::trace);

    auto execution = cosim::execution(startTime, std::make_unique<cosim::fixed_step_algorithm>(stepSize));

    auto observer = std::make_shared<cosim::time_series_observer>();
    execution.add_observer(observer);
    auto scenarioManager = std::make_shared<cosim::scenario_manager>();
    execution.add_manipulator(scenarioManager);

    auto simIndex = execution.add_slave(
        std::make_unique<mock_slave>(
            [](double x) { return x + 1.234; },
            [](int y) { return y + 2; }),
        "slave uno");

    observer->start_observing(
        cosim::variable_id{simIndex, cosim::variable_type::real, mock_slave::real_in_reference});
    observer->start_observing(
        cosim::variable_id{simIndex, cosim::variable_type::real, mock_slave::real_out_reference});
    observer->start_observing(
        cosim::variable_id{simIndex, cosim::variable_type::integer, mock_slave::integer_in_reference});
    observer->start_observing(
        cosim::variable_id{simIndex, cosim::variable_type::integer, mock_slave::integer_out_reference});

    scenarioManager->load_scenario(scenarioFile, startTime);

    auto simResult = execution.simulate_until(endTime);
    REQUIRE(simResult);

    const int numSamples = 11;
    double realInputValues[numSamples];
    double realOutputValues[numSamples];
    int intInputValues[numSamples];
    int intOutputValues[numSamples];
    cosim::step_number steps[numSamples];
    cosim::time_point times[numSamples];

    size_t samplesRead = observer->get_real_samples(
        simIndex,
        mock_slave::real_in_reference,
        1,
        gsl::make_span(realInputValues, numSamples),
        gsl::make_span(steps, numSamples),
        gsl::make_span(times, numSamples));
    CHECK(samplesRead == 11);
    samplesRead = observer->get_real_samples(
        simIndex,
        mock_slave::real_out_reference,
        1,
        gsl::make_span(realOutputValues, numSamples),
        gsl::make_span(steps, numSamples),
        gsl::make_span(times, numSamples));
    CHECK(samplesRead == 11);
    samplesRead = observer->get_integer_samples(
        simIndex,
        mock_slave::integer_in_reference,
        1,
        gsl::make_span(intInputValues, numSamples),
        gsl::make_span(steps, numSamples),
        gsl::make_span(times, numSamples));
    CHECK(samplesRead == 11);
    samplesRead = observer->get_integer_samples(
        simIndex,
        mock_slave::integer_out_reference,
        1,
        gsl::make_span(intOutputValues, numSamples),
        gsl::make_span(steps, numSamples),
        gsl::make_span(times, numSamples));
    CHECK(samplesRead == 11);

    double expectedRealInputs[] = {0.0, 0.0, 0.0, 0.0, 0.0, 1.001, 1.001, 1.001, 1.001, 1.001, 0.0};
    double expectedRealOutputs[] = {1.234, 1.234, -1.0, 1.234, 1.234, 2.235, 2.235, 2.235, 2.235, 2.235, 1.234};
    int expectedIntInputs[] = {0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 0};
    int expectedIntOutputs[] = {2, 2, 2, 2, 2, 2, 2, 4, 5, 5, 2};

    for (size_t i = 0; i < samplesRead; i++) {
        CHECK(realInputValues[i] == Approx(expectedRealInputs[i]));
        CHECK(realOutputValues[i] == Approx(expectedRealOutputs[i]));
        CHECK(intInputValues[i] == expectedIntInputs[i]);
        CHECK(intOutputValues[i] == expectedIntOutputs[i]);
    }
}

} // namespace

TEST_CASE("json_test")
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    REQUIRE(testDataDir != nullptr);

    test(cosim::filesystem::path(testDataDir) / "scenarios" / "scenario1.json");
}

TEST_CASE("yaml_test")
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    REQUIRE(testDataDir != nullptr);

    test(cosim::filesystem::path(testDataDir) / "scenarios" / "scenario1.yml");
}
