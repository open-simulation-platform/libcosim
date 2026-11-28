#include <cosim/algorithm/fixed_step_algorithm.hpp>
#include <cosim/exception.hpp>
#include <cosim/fs_portability.hpp>
#include <cosim/log/simple.hpp>
#include <cosim/manipulator/override_manipulator.hpp>
#include <cosim/observer/last_value_observer.hpp>
#include <cosim/orchestration.hpp>
#include <cosim/osp_config_parser.hpp>
#include <cosim/system_structure.hpp>
#include <cosim/time.hpp>

#include <cstdlib>
#include <cstdint>
#include <exception>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <system_error>
#include <vector>


#include "test_require.hpp"


namespace
{

constexpr auto clockInterval = cosim::to_duration(1.0);
constexpr auto nominalStepSize = cosim::to_duration(1.5);
constexpr cosim::value_reference inClock1Ticks = 2001;
constexpr cosim::value_reference result2Snapshot = 2008;


std::shared_ptr<cosim::model> load_model(
    const cosim::filesystem::path& fixtureDirectory,
    const char* filename)
{
    auto resolver = cosim::default_model_uri_resolver();
    return resolver->lookup_model(cosim::path_to_file_uri(
        fixtureDirectory / "fmus" / filename));
}


cosim::system_structure make_two_fmu_structure(
    std::shared_ptr<cosim::model> model)
{
    cosim::system_structure systemStructure;
    systemStructure.add_entity("source", model);
    systemStructure.add_entity("target", std::move(model));
    return systemStructure;
}


void run_clock_routing_test(
    const cosim::system_structure& systemStructure,
    cosim::time_point startTime,
    cosim::fixed_step_algorithm_params algorithmParameters)
{
    auto algorithm =
        std::make_shared<cosim::fixed_step_algorithm>(algorithmParameters);
    auto execution = cosim::execution(startTime, algorithm);
    auto observer = std::make_shared<cosim::last_value_observer>();
    execution.add_observer(observer);

    const auto entityMaps = cosim::inject_system_structure(
        execution, systemStructure, {});
    REQUIRE(entityMaps.simulators.size() == 2U);
    REQUIRE(
        std::distance(
            systemStructure.connections().begin(),
            systemStructure.connections().end()) == 2);

    /*
     * Clock schedule used by the generated fixture:
     *
     * time        0 1 2 3 4 5 6 7 8 9
     * inClock1    + + + + + + + + + +   input, triggered every second
     * inClock2    + +             + +   input, triggered at 0, 1, 8, 9
     * inClock3            +             input, triggered by inClock1
     * outClock    ? ? ? ? ? ? ? ? ? ?   output, triggered by all inClocks
     *                                    when totalInTicks % 5 == 0
     * time        0 1 2 3 4 5 6 7 8 9
     *
     * At t=2, the fifth combined tick raises outClock. The connections route
     * it to the target's inClock2 partition and transfer the associated
     * inClock1Ticks value before activation.
     */

    // Although the configured base step is 1.5 s, the router must stop at the
    // periodic Clock instants at t=1 and t=2.
    REQUIRE(algorithmParameters.stepSize == nominalStepSize);
    REQUIRE(execution.step() == clockInterval);
    REQUIRE(execution.current_time() == cosim::to_time_point(1.0));
    REQUIRE(execution.step() == clockInterval);
    REQUIRE(execution.current_time() == cosim::to_time_point(2.0));

    const auto source = entityMaps.simulators.at("source");
    int routedValue = 0;
    observer->get_integer(
        source,
        gsl::make_span(&inClock1Ticks, 1),
        gsl::make_span(&routedValue, 1));

    // inClock1 has activated at t=0, t=1, and t=2. Its third tick is the
    // associated value transferred with outClock at the fifth combined tick.
    REQUIRE(routedValue == 3);

    // Advance once more so the target's ordinary diagnostic output is sampled.
    // No routed outClock event occurs at t=3, so the observed associated source
    // value reflects the later activation, even without a matching output tick.
    REQUIRE(execution.step() == clockInterval);
    REQUIRE(execution.current_time() == cosim::to_time_point(3.0));

    const auto target = entityMaps.simulators.at("target");
    int targetResult = 0;
    observer->get_integer(
        target,
        gsl::make_span(&result2Snapshot, 1),
        gsl::make_span(&targetResult, 1));
    observer->get_integer(
        source,
        gsl::make_span(&inClock1Ticks, 1),
        gsl::make_span(&routedValue, 1));

    // result2Snapshot mirrors the target partition result outside Clock
    // activation. A value of 3 proves data was set before inClock2 activated.
    REQUIRE(targetResult == 3);
    REQUIRE(routedValue == 4);
}


void test_clock_xml_system_structure(
    const cosim::filesystem::path& fixtureDirectory)
{
    const auto configPath =
        fixtureDirectory / "fixed_step" / "clocks" / "OspSystemStructure.xml";
    auto resolver = cosim::default_model_uri_resolver();
    const auto config = cosim::load_osp_config(configPath, *resolver);
    const auto algorithmParameters =
        std::get<cosim::fixed_step_algorithm_params>(
            config.algorithm_configuration);

    run_clock_routing_test(
        config.system_structure,
        config.start_time,
        algorithmParameters);
}


void test_clock_system_structure_api(
    const cosim::filesystem::path& fixtureDirectory)
{
    auto systemStructure = make_two_fmu_structure(
        load_model(fixtureDirectory, "ClockedCoSimulation.fmu"));
    systemStructure.connect_variables(
        {"source", "outClock"},
        {"target", "inClock2"});
    systemStructure.connect_variables(
        {"source", "inClock1Ticks"},
        {"target", "input2"});

    run_clock_routing_test(
        systemStructure,
        cosim::to_time_point(0.0),
        {nominalStepSize});
}


struct routed_value
{
    const char* outputName;
    const char* inputName;
    cosim::variable_type type;
    cosim::value_reference outputReference;
    cosim::value_reference inputReference;
    cosim::variable_value expected;
};


std::vector<routed_value> exact_routed_values()
{
    return {
        {"Float32_continuous_output",
         "Float32_continuous_input",
         cosim::variable_type::float32,
         2,
         1,
         cosim::variable_value(cosim::float32_values{{1.25F}})},
        {"Float32_discrete_output",
         "Float32_discrete_input",
         cosim::variable_type::float32,
         4,
         3,
         cosim::variable_value(cosim::float32_values{{-3.5F}})},
        {"Float64_continuous_output",
         "Float64_continuous_input",
         cosim::variable_type::real,
         8,
         7,
         cosim::variable_value(cosim::real_values{{-2.5}})},
        {"Float64_discrete_output",
         "Float64_discrete_input",
         cosim::variable_type::real,
         10,
         9,
         cosim::variable_value(cosim::real_values{{7.75}})},
        {"Int8_output",
         "Int8_input",
         cosim::variable_type::int8,
         12,
         11,
         cosim::variable_value(cosim::int8_values{{-120}})},
        {"UInt8_output",
         "UInt8_input",
         cosim::variable_type::uint8,
         14,
         13,
         cosim::variable_value(cosim::uint8_values{{250}})},
        {"Int16_output",
         "Int16_input",
         cosim::variable_type::int16,
         16,
         15,
         cosim::variable_value(cosim::int16_values{{-30000}})},
        {"UInt16_output",
         "UInt16_input",
         cosim::variable_type::uint16,
         18,
         17,
         cosim::variable_value(cosim::uint16_values{{60000}})},
        {"Int32_output",
         "Int32_input",
         cosim::variable_type::integer,
         20,
         19,
         cosim::variable_value(cosim::integer_values{{-2000000000}})},
        {"UInt32_output",
         "UInt32_input",
         cosim::variable_type::uint32,
         22,
         21,
         cosim::variable_value(cosim::uint32_values{{4000000000U}})},
        {"Int64_output",
         "Int64_input",
         cosim::variable_type::int64,
         24,
         23,
         cosim::variable_value(cosim::int64_values{{
             (std::numeric_limits<std::int64_t>::min)() + 1}})},
        {"UInt64_output",
         "UInt64_input",
         cosim::variable_type::uint64,
         26,
         25,
         cosim::variable_value(cosim::uint64_values{{
             (std::numeric_limits<std::uint64_t>::max)() - 1}})},
        {"Boolean_output",
         "Boolean_input",
         cosim::variable_type::boolean,
         28,
         27,
         cosim::variable_value(cosim::boolean_values{{true}})},
        {"String_output",
         "String_input",
         cosim::variable_type::string,
         30,
         29,
         cosim::variable_value(
             cosim::string_values{{"FMI 3 exact routing"}})},
        {"Binary_output",
         "Binary_input",
         cosim::variable_type::binary,
         32,
         31,
         cosim::variable_value(cosim::binary_values{{
             cosim::binary_value{{0x00, 0x7f, 0xff}}}})},
        {"Enumeration_output",
         "Enumeration_input",
         cosim::variable_type::enumeration,
         34,
         33,
         cosim::variable_value(cosim::enumeration_values{{
             cosim::enumeration_value{2}}})},
    };
}


void run_exact_type_routing_test(
    const cosim::system_structure& systemStructure,
    cosim::time_point startTime,
    cosim::fixed_step_algorithm_params algorithmParameters)
{
    auto execution = cosim::execution(
        startTime,
        std::make_shared<cosim::fixed_step_algorithm>(algorithmParameters));
    auto observer = std::make_shared<cosim::last_value_observer>();
    auto manipulator = std::make_shared<cosim::override_manipulator>();
    execution.add_observer(observer);
    execution.add_manipulator(manipulator);

    const auto entityMaps =
        cosim::inject_system_structure(execution, systemStructure, {});
    const auto source = entityMaps.simulators.at("source");
    const auto target = entityMaps.simulators.at("target");
    const auto routedValues = exact_routed_values();
    REQUIRE(
        std::distance(
            systemStructure.connections().begin(),
            systemStructure.connections().end()) ==
        static_cast<std::ptrdiff_t>(routedValues.size()));

    // OSP 0.1 has no literals for the FMI 3 exact types, Binary, or
    // Enumeration. Override source outputs through the public whole-value
    // manipulator so the XML schema remains unchanged.
    for (const auto& route : routedValues) {
        manipulator->override_variable(
            source,
            route.type,
            route.outputReference,
            route.expected);
    }

    // The first step transfers source outputs to target inputs. The second
    // applies those inputs to the target FMU and makes them observable.
    REQUIRE(execution.step() == algorithmParameters.stepSize);
    REQUIRE(execution.step() == algorithmParameters.stepSize);

    for (const auto& route : routedValues) {
        REQUIRE(
            observer->get_variable_value(
                target,
                route.type,
                route.inputReference) == route.expected);
    }
}


void test_exact_types_xml(const cosim::filesystem::path& fixtureDirectory)
{
    auto resolver = cosim::default_model_uri_resolver();
    const auto config = cosim::load_osp_config(
        fixtureDirectory / "fixed_step" / "exact_types" /
            "OspSystemStructure.xml",
        *resolver);
    run_exact_type_routing_test(
        config.system_structure,
        config.start_time,
        std::get<cosim::fixed_step_algorithm_params>(
            config.algorithm_configuration));
}


void test_exact_types_system_structure_api(
    const cosim::filesystem::path& fixtureDirectory)
{
    auto systemStructure =
        make_two_fmu_structure(load_model(fixtureDirectory, "Feedthrough.fmu"));
    for (const auto& route : exact_routed_values()) {
        systemStructure.connect_variables(
            {"source", route.outputName},
            {"target", route.inputName});
    }
    run_exact_type_routing_test(
        systemStructure,
        cosim::to_time_point(0.0),
        {cosim::to_duration(0.1)});
}


void run_array_routing_test(
    const cosim::system_structure& systemStructure,
    cosim::time_point startTime,
    cosim::fixed_step_algorithm_params algorithmParameters)
{
    auto execution = cosim::execution(
        startTime,
        std::make_shared<cosim::fixed_step_algorithm>(algorithmParameters));
    auto observer = std::make_shared<cosim::last_value_observer>();
    auto manipulator = std::make_shared<cosim::override_manipulator>();
    execution.add_observer(observer);
    execution.add_manipulator(manipulator);

    const auto entityMaps =
        cosim::inject_system_structure(execution, systemStructure, {});
    const auto source = entityMaps.simulators.at("source");
    const auto target = entityMaps.simulators.at("target");
    const cosim::variable_value expected(
        cosim::real_values{{1.25, -2.5, 3.75}},
        {3});
    manipulator->override_variable(
        source,
        cosim::variable_type::real,
        10,
        expected);

    REQUIRE(execution.step() == algorithmParameters.stepSize);
    REQUIRE(execution.step() == algorithmParameters.stepSize);
    REQUIRE(
        observer->get_variable_value(
            target,
            cosim::variable_type::real,
            9) == expected);
}


void test_arrays_xml(const cosim::filesystem::path& fixtureDirectory)
{
    auto resolver = cosim::default_model_uri_resolver();
    const auto config = cosim::load_osp_config(
        fixtureDirectory / "fixed_step" / "arrays" /
            "OspSystemStructure.xml",
        *resolver);
    run_array_routing_test(
        config.system_structure,
        config.start_time,
        std::get<cosim::fixed_step_algorithm_params>(
            config.algorithm_configuration));
}


void test_arrays_system_structure_api(
    const cosim::filesystem::path& fixtureDirectory)
{
    auto systemStructure =
        make_two_fmu_structure(load_model(fixtureDirectory, "StateSpace.fmu"));
    systemStructure.connect_variables({"source", "y"}, {"target", "u"});
    run_array_routing_test(
        systemStructure,
        cosim::to_time_point(0.0),
        {cosim::to_duration(1.0)});
}


int observed_integer(
    cosim::last_value_observer& observer,
    cosim::simulator_index simulator,
    cosim::value_reference reference)
{
    int value = 0;
    observer.get_integer(
        simulator,
        gsl::make_span(&reference, 1),
        gsl::make_span(&value, 1));
    return value;
}


void run_discrete_event_test(
    const cosim::system_structure& systemStructure,
    cosim::time_point startTime,
    cosim::fixed_step_algorithm_params algorithmParameters)
{
    auto execution = cosim::execution(
        startTime,
        std::make_shared<cosim::fixed_step_algorithm>(algorithmParameters));
    auto observer = std::make_shared<cosim::last_value_observer>();
    execution.add_observer(observer);

    const auto entityMaps =
        cosim::inject_system_structure(execution, systemStructure, {});
    const auto source = entityMaps.simulators.at("source");

    // Stair declares a time event every second. Its discrete counter changes
    // once whenever execution lands on one of those event instants.
    REQUIRE(execution.step() == cosim::to_duration(1.0));
    REQUIRE(execution.current_time() == cosim::to_time_point(1.0));
    REQUIRE(observed_integer(*observer, source, 1) == 2);

    REQUIRE(execution.step() == cosim::to_duration(1.0));
    REQUIRE(execution.current_time() == cosim::to_time_point(2.0));
    REQUIRE(observed_integer(*observer, source, 1) == 3);
}


void test_discrete_events_xml(
    const cosim::filesystem::path& fixtureDirectory)
{
    auto resolver = cosim::default_model_uri_resolver();
    const auto config = cosim::load_osp_config(
        fixtureDirectory / "fixed_step" / "discrete_events" /
            "OspSystemStructure.xml",
        *resolver);
    run_discrete_event_test(
        config.system_structure,
        config.start_time,
        std::get<cosim::fixed_step_algorithm_params>(
            config.algorithm_configuration));
}


void test_discrete_events_system_structure_api(
    const cosim::filesystem::path& fixtureDirectory)
{
    cosim::system_structure systemStructure;
    systemStructure.add_entity(
        "source", load_model(fixtureDirectory, "Stair.fmu"));
    run_discrete_event_test(
        systemStructure,
        cosim::to_time_point(0.0),
        {cosim::to_duration(1.0)});
}


void require_invalid_xml(
    const cosim::filesystem::path& fixtureDirectory,
    const char* filename,
    std::error_code expectedError)
{
    bool rejected = false;
    try {
        auto resolver = cosim::default_model_uri_resolver();
        const auto config = cosim::load_osp_config(
            fixtureDirectory / "invalid" / filename,
            *resolver);
        auto execution = cosim::execution(
            config.start_time,
            std::make_shared<cosim::fixed_step_algorithm>(
                std::get<cosim::fixed_step_algorithm_params>(
                    config.algorithm_configuration)));
        (void)cosim::inject_system_structure(
            execution, config.system_structure, config.initial_values);
    } catch (const cosim::error& error) {
        REQUIRE(error.code() == expectedError);
        rejected = true;
    }
    REQUIRE(rejected);
}


void test_invalid_xml(const cosim::filesystem::path& fixtureDirectory)
{
    const auto invalidStructure =
        cosim::make_error_code(cosim::errc::invalid_system_structure);
    require_invalid_xml(
        fixtureDirectory, "mismatched_exact_types.xml", invalidStructure);
    require_invalid_xml(
        fixtureDirectory, "binary_to_string.xml", invalidStructure);
    require_invalid_xml(
        fixtureDirectory, "scalar_to_array.xml", invalidStructure);
    require_invalid_xml(
        fixtureDirectory, "clock_to_data.xml", invalidStructure);
    require_invalid_xml(
        fixtureDirectory, "invalid_clock_causality.xml", invalidStructure);
    require_invalid_xml(
        fixtureDirectory, "triggered_to_periodic_clock.xml", invalidStructure);
    require_invalid_xml(
        fixtureDirectory,
        "scheduled_execution_only.xml",
        cosim::make_error_code(cosim::errc::unsupported_feature));
}


} // namespace


int main()
{
    try {
        const auto* fixtureDirectory =
            std::getenv("FMI3_SYSTEM_STRUCTURE_DIR");
        REQUIRE(fixtureDirectory);

        cosim::log::setup_simple_console_logging();
        cosim::log::set_global_output_level(cosim::log::info);

        const auto fixturePath = cosim::filesystem::path(fixtureDirectory);

        // Exercise the user-facing OSP loader, including XML parsing and
        // relative FMU resolution from the staged fixture directory.
        test_clock_xml_system_structure(fixturePath);

        // Exercise the same FMI 3 integration through the public
        // cosim::system_structure construction API.
        test_clock_system_structure_api(fixturePath);

        test_exact_types_xml(fixturePath);
        test_exact_types_system_structure_api(fixturePath);
        test_arrays_xml(fixturePath);
        test_arrays_system_structure_api(fixturePath);
        test_discrete_events_xml(fixturePath);
        test_discrete_events_system_structure_api(fixturePath);
        test_invalid_xml(fixturePath);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
