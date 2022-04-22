#define BOOST_TEST_MODULE function unittests
#include "mock_slave.hpp"

#include <cosim/algorithm/fixed_step_algorithm.hpp>
#include <cosim/function/linear_transformation.hpp>
#include <cosim/function/vector_sum.hpp>
#include <cosim/manipulator/scenario_manager.hpp>
#include <cosim/observer/time_series_observer.hpp>

#include <boost/test/unit_test.hpp>

#include <iostream>
#include <stdexcept>
#include <utility>

int find_param(const cosim::function_type_description& ftd, const std::string& name)
{
    for (int i = 0; i < static_cast<int>(ftd.parameters.size()); ++i) {
        if (ftd.parameters[i].name == name) return i;
    }
    throw std::logic_error("Parameter not found: " + name);
}


std::pair<int, int> find_io(
    const cosim::function_description& fd,
    const std::string& groupName,
    const std::string& varName = "")
{
    for (int g = 0; g < static_cast<int>(fd.io_groups.size()); ++g) {
        const auto& group = fd.io_groups[g];
        if (group.name != groupName) continue;
        for (int v = 0; v < static_cast<int>(group.ios.size()); ++v) {
            if (group.ios[v].name == varName) return {g, v};
        }
    }
    throw std::logic_error("Variable not found: " + groupName + ":" + varName);
}


BOOST_AUTO_TEST_CASE(linear_transformation_standalone)
{
    constexpr double offset = 3.0;
    constexpr double factor = 5.0;

    auto type = cosim::linear_transformation_function_type();
    const auto typeDesc = type.description();

    std::unordered_map<int, cosim::function_parameter_value> params;
    params[find_param(typeDesc, "offset")] = offset;
    params[find_param(typeDesc, "factor")] = factor;

    const auto fun = type.instantiate(params);
    const auto funDesc = fun->description();
    BOOST_TEST_REQUIRE(funDesc.io_groups.size() == 2);
    BOOST_TEST_REQUIRE(funDesc.io_groups[0].ios.size() == 1);
    BOOST_TEST_REQUIRE(funDesc.io_groups[1].ios.size() == 1);

    const auto [inGID, inVID] = find_io(funDesc, "in");
    const auto [outGID, outVID] = find_io(funDesc, "out");

    fun->set_real({inGID, 0, inVID, 0}, 10.0);
    fun->calculate();
    BOOST_TEST(fun->get_real({outGID, 0, outVID, 0}) == 53.0);

    fun->set_real({inGID, 0, inVID, 0}, -1.0);
    fun->calculate();
    BOOST_TEST(fun->get_real({outGID, 0, outVID, 0}) == -2.0);
}


BOOST_AUTO_TEST_CASE(vector_sum_standalone)
{
    constexpr int inputCount = 3;
    constexpr auto numericType = cosim::variable_type::integer;
    constexpr int dimension = 2;

    auto type = cosim::vector_sum_function_type();
    const auto typeDesc = type.description();

    std::unordered_map<int, cosim::function_parameter_value> params;
    params[find_param(typeDesc, "inputCount")] = inputCount;
    params[find_param(typeDesc, "numericType")] = numericType;
    params[find_param(typeDesc, "dimension")] = dimension;

    const auto fun = type.instantiate(params);
    const auto funDesc = fun->description();
    BOOST_TEST_REQUIRE(funDesc.io_groups.size() == 2);
    BOOST_TEST_REQUIRE(funDesc.io_groups[0].ios.size() == 1);
    BOOST_TEST_REQUIRE(funDesc.io_groups[1].ios.size() == 1);
    BOOST_TEST(std::get<int>(funDesc.io_groups[0].count) == inputCount);
    BOOST_TEST(std::get<int>(funDesc.io_groups[1].count) == 1);
    BOOST_TEST(std::get<cosim::variable_type>(funDesc.io_groups[0].ios[0].type) == numericType);
    BOOST_TEST(std::get<cosim::variable_type>(funDesc.io_groups[1].ios[0].type) == numericType);
    BOOST_TEST(std::get<int>(funDesc.io_groups[0].ios[0].count) == dimension);
    BOOST_TEST(std::get<int>(funDesc.io_groups[1].ios[0].count) == dimension);

    const auto [inGID, inVID] = find_io(funDesc, "in");
    const auto [outGID, outVID] = find_io(funDesc, "out");
    fun->set_integer({inGID, 0, inVID, 0}, 1);
    fun->set_integer({inGID, 0, inVID, 1}, 2);
    fun->set_integer({inGID, 1, inVID, 0}, 3);
    fun->set_integer({inGID, 1, inVID, 1}, 5);
    fun->set_integer({inGID, 2, inVID, 0}, 7);
    fun->set_integer({inGID, 2, inVID, 1}, 11);
    fun->calculate();
    BOOST_TEST(fun->get_integer({outGID, 0, outVID, 0}) == 11);
    BOOST_TEST(fun->get_integer({outGID, 0, outVID, 1}) == 18);
}


BOOST_AUTO_TEST_CASE(function_in_execution)
{
    // The system simulated here looks like this:
    //
    //     .----.    .----------------------------.    .----.
    //     | S1 |--->| out = offset + factor * in |--->| S2 |
    //     '----'    '----------------------------'    '----'
    //
    // S1 and S2 are simple "identity" simulators, i.e., their output
    // is equal to their input.  We set S1's output via its input, and
    // we read S2's input via its output.
    //
    // S1's output starts off as zero, and then we change it twice
    // during the simulation.

    // Test parameters
    constexpr auto offset = 1.0;
    constexpr auto factor = 2.0;
    constexpr auto startTime = cosim::time_point();
    constexpr auto stopTime = cosim::time_point(std::chrono::seconds(3));
    constexpr auto stepSize = std::chrono::milliseconds(100);
    constexpr auto event1Time = cosim::time_point(std::chrono::seconds(1));
    constexpr auto event1Value = 10.0;
    constexpr auto event2Time = cosim::time_point(std::chrono::seconds(2));
    constexpr auto event2Value = -10.0;
    constexpr auto bufferSize = (stopTime - startTime) / stepSize + 1;

    // Variable IDs for mock_slave
    constexpr auto mockRealOut = 0;
    constexpr auto mockRealIn = 1;

    auto exe = cosim::execution(
        startTime,
        std::make_shared<cosim::fixed_step_algorithm>(stepSize));

    // Set up time series observer and scenario manager
    const auto observer = std::make_shared<cosim::time_series_observer>(bufferSize);
    exe.add_observer(observer);
    const auto scenarioManager = std::make_shared<cosim::scenario_manager>();
    exe.add_manipulator(scenarioManager);

    // Set up slaves and function
    const auto s1 = std::make_shared<mock_slave>();
    const auto idS1 = exe.add_slave(s1, "S1");
    const auto f = std::make_shared<cosim::linear_transformation_function>(offset, factor);
    const auto idF = exe.add_function(f);
    const auto s2 = std::make_shared<mock_slave>();
    const auto idS2 = exe.add_slave(s2, "S2");
    exe.connect_variables(
        cosim::variable_id{idS1, cosim::variable_type::real, mockRealOut},
        cosim::function_io_id{idF, cosim::variable_type::real, cosim::linear_transformation_function::in_io_reference});
    exe.connect_variables(
        cosim::function_io_id{idF, cosim::variable_type::real, cosim::linear_transformation_function::out_io_reference},
        cosim::variable_id{idS2, cosim::variable_type::real, mockRealIn});

    // Set up a scenario that modifies S1's input value, and thereby indirectly
    // its output value.
    cosim::scenario::scenario scenario;
    scenario.events.push_back(
        cosim::scenario::event{
            event1Time,
            cosim::scenario::variable_action{
                idS1,
                mockRealIn,
                cosim::scenario::real_modifier{[=](double, cosim::duration) { return event1Value; }},
                true}});
    scenario.events.push_back(
        cosim::scenario::event{
            event2Time,
            cosim::scenario::variable_action{
                idS1,
                mockRealIn,
                cosim::scenario::real_modifier{[=](double, cosim::duration) { return event2Value; }},
                true}});
    scenario.end = stopTime;
    scenarioManager->load_scenario(scenario, exe.current_time());

    // Collect S2's output values
    observer->start_observing(
        cosim::variable_id{idS2, cosim::variable_type::real, mockRealOut});

    // Run simulation and verify results
    exe.simulate_until(stopTime);

    auto outputs = std::vector<double>(bufferSize);
    auto steps = std::vector<cosim::step_number>(bufferSize);
    auto times = std::vector<cosim::time_point>(bufferSize);
    observer->get_real_samples(
        idS2, mockRealOut, 0,
        gsl::make_span(outputs),
        gsl::make_span(steps),
        gsl::make_span(times));
    for (int i = 0; i < bufferSize; ++i) std::cout << steps[i] << "  " << cosim::to_double_time_point(times[i]) << "  " << outputs[i] << std::endl;

    // Check outputs at times halfway between startTime and event1Time, between
    // event1Time and event2Time, and between event2Time and stopTime.
    constexpr auto midpoint1 =
        (startTime.time_since_epoch() + event1Time.time_since_epoch()) /
        (2 * stepSize);
    BOOST_TEST(outputs.at(midpoint1) == 1.0); // Result when input is 0.0

    constexpr auto midpoint2 =
        (event1Time.time_since_epoch() + event2Time.time_since_epoch()) /
        (2 * stepSize);
    BOOST_TEST(outputs.at(midpoint2) == 21.0); // Result when input is 10.0

    constexpr auto midpoint3 =
        (event2Time.time_since_epoch() + stopTime.time_since_epoch()) /
        (2 * stepSize);
    BOOST_TEST(outputs.at(midpoint3) == -19.0); // Result when input is -10.0
}
