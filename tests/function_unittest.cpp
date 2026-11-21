#define BOOST_TEST_MODULE function unittests
#include "exact_array_function.hpp"
#include "mock_slave.hpp"

#include <cosim/algorithm/fixed_step_algorithm.hpp>
#include <cosim/function/linear_transformation.hpp>
#include <cosim/function/utility.hpp>
#include <cosim/function/vector_sum.hpp>
#include <cosim/manipulator/scenario_manager.hpp>
#include <cosim/observer/time_series_observer.hpp>

#include <boost/test/unit_test.hpp>

#include <array>
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

    fun->set_variable_value(
        {inGID, 0, inVID, 0},
        cosim::variable_value(cosim::real_values{{4.0}}));
    fun->calculate();
    BOOST_CHECK(
        fun->get_variable_value({outGID, 0, outVID, 0}) ==
        cosim::variable_value(cosim::real_values{{23.0}}));
    BOOST_CHECK_THROW(
        fun->set_variable_value(
            {inGID, 0, inVID, 0},
            cosim::variable_value(cosim::real_values{{4.0}}, {1})),
        std::invalid_argument);
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


BOOST_AUTO_TEST_CASE(parameter_bounds_diagnostics)
{
    cosim::function_type_description description;
    description.parameters = {
        {"count", cosim::function_parameter_type::integer, 2, 0, 4}};
    description.io_groups = {
        {"values", cosim::function_parameter_placeholder{0}, {}}};

    for (const int value : {0, 4}) {
        const auto result = cosim::substitute_function_parameters(description, {{0, value}});
        BOOST_TEST(std::get<int>(result.io_groups[0].count) == value);
    }
    BOOST_CHECK_EXCEPTION(
        cosim::substitute_function_parameters(description, {{0, -1}}),
        std::domain_error,
        [](const std::domain_error& error) {
            return std::string(error.what()).find("too small") != std::string::npos;
        });
    BOOST_CHECK_EXCEPTION(
        cosim::substitute_function_parameters(description, {{0, 5}}),
        std::domain_error,
        [](const std::domain_error& error) {
            return std::string(error.what()).find("too large") != std::string::npos;
        });
}

BOOST_AUTO_TEST_CASE(parameterized_function_dimensions)
{
    cosim::function_type_description typeDescription;
    typeDescription.parameters = {
        {"dimension",
            cosim::function_parameter_type::integer,
            2,
            0,
            {}}};
    typeDescription.io_groups = {
        {"values",
            1,
            {{"",
                cosim::variable_type::uint64,
                cosim::variable_causality::input,
                3,
                {cosim::function_parameter_placeholder{0}}}}}};

    const auto defaultDescription =
        cosim::substitute_function_parameters(typeDescription, {});
    const auto& defaultIo = defaultDescription.io_groups[0].ios[0];
    BOOST_TEST(std::get<int>(defaultIo.count) == 3);
    BOOST_TEST(std::get<std::uint64_t>(defaultIo.dimensions[0]) == 2);

    const auto configuredDescription = cosim::substitute_function_parameters(
        typeDescription,
        {{0, 4}});
    const auto& configuredIo = configuredDescription.io_groups[0].ios[0];
    BOOST_TEST(std::get<int>(configuredIo.count) == 3);
    BOOST_TEST(std::get<std::uint64_t>(configuredIo.dimensions[0]) == 4);
    BOOST_CHECK_THROW(
        cosim::substitute_function_parameters(
            typeDescription,
            {{0, -1}}),
        std::domain_error);
}


BOOST_AUTO_TEST_CASE(exact_array_function_endpoints)
{
    exact_array_function function;
    const auto description = function.description();
    const auto& input = description.io_groups[0].ios[0];
    BOOST_TEST(std::get<int>(description.io_groups[0].count) == 2);
    BOOST_TEST(std::get<int>(input.count) == 1);
    BOOST_TEST(std::get<std::uint64_t>(input.dimensions[0]) == 2);

    function.set_variable_value(
        exact_array_function::input_reference(0),
        cosim::variable_value(cosim::uint64_values{{1, 2}}, {2}));
    function.set_variable_value(
        exact_array_function::input_reference(1),
        cosim::variable_value(cosim::uint64_values{{3, 5}}, {2}));
    function.calculate();
    BOOST_CHECK(
        function.get_variable_value(exact_array_function::output_reference) ==
        cosim::variable_value(cosim::uint64_values{{4, 7}}, {2}));
    BOOST_CHECK_THROW(
        function.set_variable_value(
            exact_array_function::input_reference(0),
            cosim::variable_value(cosim::uint64_values{{1}}, {1})),
        std::invalid_argument);
}


BOOST_AUTO_TEST_CASE(exact_array_connections_in_fixed_step_algorithm)
{
    const cosim::variable_value first(
        cosim::uint64_values{{10, 20}}, {2});
    const cosim::variable_value second(
        cosim::uint64_values{{1, 2}}, {2});
    const cosim::variable_value third(
        cosim::uint64_values{{100, 200}}, {2});
    const cosim::variable_value expected(
        cosim::uint64_values{{111, 222}}, {2});

    auto firstSource = std::make_shared<whole_value_mock_slave>(first);
    auto secondSource = std::make_shared<whole_value_mock_slave>(second);
    auto thirdSource = std::make_shared<whole_value_mock_slave>(third);
    auto target = std::make_shared<whole_value_mock_slave>(
        cosim::variable_value(cosim::uint64_values{{0, 0}}, {2}));
    auto firstFunction = std::make_shared<exact_array_function>();
    auto secondFunction = std::make_shared<exact_array_function>();

    cosim::execution execution(
        cosim::time_point(),
        std::make_shared<cosim::fixed_step_algorithm>(
            std::chrono::milliseconds(10)));
    const auto firstSourceIndex =
        execution.add_slave(firstSource, "first-source");
    const auto secondSourceIndex =
        execution.add_slave(secondSource, "second-source");
    const auto thirdSourceIndex =
        execution.add_slave(thirdSource, "third-source");
    const auto targetIndex = execution.add_slave(target, "target");
    const auto firstFunctionIndex = execution.add_function(firstFunction);
    const auto secondFunctionIndex = execution.add_function(secondFunction);

    const auto source = [](cosim::simulator_index index) {
        return cosim::variable_id{
            index,
            cosim::variable_type::uint64,
            whole_value_mock_slave::output_reference};
    };
    const auto functionInput = [](cosim::function_index index, int instance) {
        return cosim::function_io_id{
            index,
            cosim::variable_type::uint64,
            exact_array_function::input_reference(instance)};
    };
    const auto functionOutput = [](cosim::function_index index) {
        return cosim::function_io_id{
            index,
            cosim::variable_type::uint64,
            exact_array_function::output_reference};
    };

    execution.connect_variables(
        source(firstSourceIndex), functionInput(firstFunctionIndex, 0));
    execution.connect_variables(
        source(secondSourceIndex), functionInput(firstFunctionIndex, 1));
    execution.connect_variables(
        functionOutput(firstFunctionIndex),
        functionInput(secondFunctionIndex, 0));
    execution.connect_variables(
        source(thirdSourceIndex), functionInput(secondFunctionIndex, 1));
    execution.connect_variables(
        functionOutput(secondFunctionIndex),
        cosim::variable_id{
            targetIndex,
            cosim::variable_type::uint64,
            whole_value_mock_slave::input_reference});

    execution.initialize();
    execution.step();
    BOOST_CHECK(target->input_value() == expected);
}


BOOST_AUTO_TEST_CASE(connection_shape_and_reference_validation)
{
    auto source = std::make_shared<whole_value_mock_slave>(
        cosim::variable_value(cosim::uint64_values{{1, 2}}, {2}));
    auto wrongShape = std::make_shared<whole_value_mock_slave>(
        cosim::variable_value(cosim::uint64_values{{0, 0, 0}}, {3}));
    auto function = std::make_shared<exact_array_function>();
    cosim::execution execution(
        cosim::time_point(),
        std::make_shared<cosim::fixed_step_algorithm>(
            std::chrono::milliseconds(10)));
    const auto sourceIndex = execution.add_slave(source, "source");
    const auto targetIndex = execution.add_slave(wrongShape, "wrong-shape");
    const auto functionIndex = execution.add_function(function);

    BOOST_CHECK_THROW(
        execution.connect_variables(
            cosim::variable_id{
                sourceIndex,
                cosim::variable_type::uint64,
                whole_value_mock_slave::output_reference},
            cosim::variable_id{
                targetIndex,
                cosim::variable_type::uint64,
                whole_value_mock_slave::input_reference}),
        std::invalid_argument);
    BOOST_CHECK_THROW(
        execution.connect_variables(
            cosim::variable_id{
                sourceIndex,
                cosim::variable_type::uint64,
                whole_value_mock_slave::output_reference},
            cosim::function_io_id{
                functionIndex,
                cosim::variable_type::uint64,
                {0, 2, 0, 0}}),
        std::out_of_range);
    BOOST_CHECK_THROW(
        execution.connect_variables(
            cosim::variable_id{
                sourceIndex,
                cosim::variable_type::uint64,
                whole_value_mock_slave::output_reference},
            cosim::function_io_id{
                functionIndex,
                cosim::variable_type::int64,
                exact_array_function::input_reference(0)}),
        std::logic_error);
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
    auto success = exe.simulate_until(stopTime);
    BOOST_TEST_REQUIRE(success);

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
