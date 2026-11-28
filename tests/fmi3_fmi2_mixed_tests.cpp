#define BOOST_TEST_MODULE FMI 3 FMI 2 mixed tests

#include <cosim/algorithm/fixed_step_algorithm.hpp>
#include <cosim/exception.hpp>
#include <cosim/fs_portability.hpp>
#include <cosim/manipulator/override_manipulator.hpp>
#include <cosim/manipulator/scenario_manager.hpp>
#include <cosim/model_description.hpp>
#include <cosim/observer/last_value_observer.hpp>
#include <cosim/orchestration.hpp>
#include <cosim/osp_config_parser.hpp>
#include <cosim/system_structure.hpp>
#include <cosim/time.hpp>

#include <boost/test/unit_test.hpp>

#include <cstdlib>
#include <memory>


BOOST_AUTO_TEST_CASE(fmi2_enumeration_osp_integer_initial_value)
{
    const auto* fixtureDirectory = std::getenv("FMI3_SYSTEM_STRUCTURE_DIR");
    BOOST_REQUIRE(fixtureDirectory != nullptr);
    auto resolver = cosim::default_model_uri_resolver();
    const auto config = cosim::load_osp_config(
        cosim::filesystem::path(fixtureDirectory) / "fixed_step" /
            "enumeration_initial" / "OspSystemStructure.xml",
        *resolver);
    const auto& initial = config.initial_values.at({"fmi2", "enumeration_input"});
    BOOST_TEST(initial.type() == cosim::variable_type::enumeration);
    BOOST_TEST(std::get<cosim::enumeration_values>(initial.values()).values.at(0).value == 2);

    const auto parameters = std::get<cosim::fixed_step_algorithm_params>(
        config.algorithm_configuration);
    auto execution = cosim::execution(
        config.start_time,
        std::make_shared<cosim::fixed_step_algorithm>(parameters));
    auto observer = std::make_shared<cosim::last_value_observer>();
    execution.add_observer(observer);
    const auto entities = cosim::inject_system_structure(
        execution, config.system_structure, config.initial_values);
    const auto simulator = entities.simulators.at("fmi2");
    const auto output = cosim::find_variable(
        execution.get_model_description(simulator), "enumeration_output");
    BOOST_REQUIRE(output);
    BOOST_CHECK(execution.simulate_until(cosim::to_time_point(0.2)));
    const auto observed = observer->get_variable_value(
        simulator, cosim::variable_type::enumeration, output->reference);
    BOOST_TEST(observed.type() == cosim::variable_type::enumeration);
    BOOST_TEST(std::get<cosim::enumeration_values>(observed.values())
        .values[0].value == 2);
}


BOOST_AUTO_TEST_CASE(fmi2_real_and_fmi3_float64_route_bidirectionally)
{
    const auto* fixtureDirectory =
        std::getenv("FMI3_SYSTEM_STRUCTURE_DIR");
    BOOST_REQUIRE(fixtureDirectory != nullptr);
    const auto* testDataDirectory = std::getenv("TEST_DATA_DIR");
    BOOST_REQUIRE(testDataDirectory != nullptr);

    auto resolver = cosim::default_model_uri_resolver();
    const auto config = cosim::load_osp_config(
        cosim::filesystem::path(fixtureDirectory) / "fixed_step" / "mixed" /
            "OspSystemStructure.xml",
        *resolver);
    const auto stepSize = std::get<cosim::fixed_step_algorithm_params>(
                              config.algorithm_configuration)
                              .stepSize;
    auto execution = cosim::execution(
        config.start_time,
        std::make_shared<cosim::fixed_step_algorithm>(stepSize));
    auto observer = std::make_shared<cosim::last_value_observer>();
    auto manipulator = std::make_shared<cosim::override_manipulator>();
    auto scenario = std::make_shared<cosim::scenario_manager>();
    execution.add_observer(observer);
    execution.add_manipulator(manipulator);
    execution.add_manipulator(scenario);

    const auto entities = cosim::inject_system_structure(
        execution, config.system_structure, config.initial_values);
    const auto fmi2 = entities.simulators.at("fmi2");
    const auto fmi3 = entities.simulators.at("fmi3");

    const auto fmi2Output =
        cosim::find_variable(execution.get_model_description(fmi2), "output[0]");
    const auto fmi2Input =
        cosim::find_variable(execution.get_model_description(fmi2), "input[1]");
    const auto fmi3Input = cosim::find_variable(
        execution.get_model_description(fmi3), "Float64_discrete_input");
    const auto fmi3Output = cosim::find_variable(
        execution.get_model_description(fmi3), "Float64_continuous_output");
    BOOST_REQUIRE(fmi2Output);
    BOOST_REQUIRE(fmi2Input);
    BOOST_REQUIRE(fmi3Input);
    BOOST_REQUIRE(fmi3Output);
    BOOST_TEST(fmi2Output->type == cosim::variable_type::real);
    BOOST_TEST(fmi2Input->type == cosim::variable_type::real);
    BOOST_TEST(fmi3Input->type == cosim::variable_type::real);
    BOOST_TEST(fmi3Output->type == cosim::variable_type::real);

    constexpr double fmi2ToFmi3 = 12.5;
    constexpr double fmi3ToFmi2 = -4.25;
    manipulator->override_variable(
        fmi2,
        cosim::variable_type::real,
        3,
        cosim::variable_value(cosim::real_values{{fmi2ToFmi3}}));
    scenario->load_scenario(
        cosim::filesystem::path(testDataDirectory) / "scenarios" /
            "fmi3_float64.yml",
        config.start_time);

    BOOST_CHECK(execution.step() == stepSize);
    BOOST_CHECK(execution.step() == stepSize);

    const auto fmi3Value = observer->get_variable_value(
        fmi3, cosim::variable_type::real, 10);
    BOOST_TEST(
        std::get<cosim::real_values>(fmi3Value.values()).values.at(0) ==
        fmi2ToFmi3);

    const auto fmi2Value = observer->get_variable_value(
        fmi2, cosim::variable_type::real, 4);
    BOOST_TEST(
        std::get<cosim::real_values>(fmi2Value.values()).values.at(0) ==
        fmi3ToFmi2);
}


BOOST_AUTO_TEST_CASE(fmi2_integer_boolean_and_fmi3_exact_types_route_bidirectionally)
{
    const auto* fixtureDirectory =
        std::getenv("FMI3_SYSTEM_STRUCTURE_DIR");
    BOOST_REQUIRE(fixtureDirectory != nullptr);

    auto resolver = cosim::default_model_uri_resolver();
    const auto config = cosim::load_osp_config(
        cosim::filesystem::path(fixtureDirectory) / "fixed_step" /
            "mixed_types" / "OspSystemStructure.xml",
        *resolver);
    const auto stepSize = std::get<cosim::fixed_step_algorithm_params>(
                              config.algorithm_configuration)
                              .stepSize;
    auto execution = cosim::execution(
        config.start_time,
        std::make_shared<cosim::fixed_step_algorithm>(stepSize));
    auto observer = std::make_shared<cosim::last_value_observer>();
    auto manipulator = std::make_shared<cosim::override_manipulator>();
    execution.add_observer(observer);
    execution.add_manipulator(manipulator);

    const auto entities = cosim::inject_system_structure(
        execution, config.system_structure, config.initial_values);
    const auto fmi2 = entities.simulators.at("fmi2");
    const auto fmi3 = entities.simulators.at("fmi3");

    const auto fmi2IntegerInput = cosim::find_variable(
        execution.get_model_description(fmi2), "integer_input");
    const auto fmi2IntegerOutput = cosim::find_variable(
        execution.get_model_description(fmi2), "integer_output");
    const auto fmi3IntegerInput = cosim::find_variable(
        execution.get_model_description(fmi3), "Int32_input");
    const auto fmi3IntegerOutput = cosim::find_variable(
        execution.get_model_description(fmi3), "Int32_output");
    const auto fmi2BooleanInput = cosim::find_variable(
        execution.get_model_description(fmi2), "boolean_input");
    const auto fmi2BooleanOutput = cosim::find_variable(
        execution.get_model_description(fmi2), "boolean_output");
    const auto fmi3BooleanInput = cosim::find_variable(
        execution.get_model_description(fmi3), "Boolean_input");
    const auto fmi3BooleanOutput = cosim::find_variable(
        execution.get_model_description(fmi3), "Boolean_output");
    const auto fmi2EnumerationInput = cosim::find_variable(
        execution.get_model_description(fmi2), "enumeration_input");
    const auto fmi2EnumerationOutput = cosim::find_variable(
        execution.get_model_description(fmi2), "enumeration_output");
    const auto fmi3EnumerationInput = cosim::find_variable(
        execution.get_model_description(fmi3), "Enumeration_input");
    const auto fmi3EnumerationOutput = cosim::find_variable(
        execution.get_model_description(fmi3), "Enumeration_output");
    BOOST_REQUIRE(fmi2IntegerInput);
    BOOST_REQUIRE(fmi2IntegerOutput);
    BOOST_REQUIRE(fmi3IntegerInput);
    BOOST_REQUIRE(fmi3IntegerOutput);
    BOOST_REQUIRE(fmi2BooleanInput);
    BOOST_REQUIRE(fmi2BooleanOutput);
    BOOST_REQUIRE(fmi3BooleanInput);
    BOOST_REQUIRE(fmi3BooleanOutput);
    BOOST_REQUIRE(fmi2EnumerationInput);
    BOOST_REQUIRE(fmi2EnumerationOutput);
    BOOST_REQUIRE(fmi3EnumerationInput);
    BOOST_REQUIRE(fmi3EnumerationOutput);
    BOOST_TEST(fmi2IntegerInput->type == cosim::variable_type::integer);
    BOOST_TEST(fmi2IntegerOutput->type == cosim::variable_type::integer);
    BOOST_TEST(fmi3IntegerInput->type == cosim::variable_type::integer);
    BOOST_TEST(fmi3IntegerOutput->type == cosim::variable_type::integer);
    BOOST_TEST(fmi2BooleanInput->type == cosim::variable_type::boolean);
    BOOST_TEST(fmi2BooleanOutput->type == cosim::variable_type::boolean);
    BOOST_TEST(fmi3BooleanInput->type == cosim::variable_type::boolean);
    BOOST_TEST(fmi3BooleanOutput->type == cosim::variable_type::boolean);
    BOOST_TEST(
        fmi2EnumerationInput->type == cosim::variable_type::enumeration);
    BOOST_TEST(
        fmi2EnumerationOutput->type == cosim::variable_type::enumeration);
    BOOST_TEST(
        fmi3EnumerationInput->type == cosim::variable_type::enumeration);
    BOOST_TEST(
        fmi3EnumerationOutput->type == cosim::variable_type::enumeration);

    constexpr int fmi2ToFmi3Integer = 37;
    constexpr int fmi3ToFmi2Integer = -11;
    constexpr bool fmi2ToFmi3Boolean = true;
    constexpr bool fmi3ToFmi2Boolean = false;
    manipulator->override_integer_variable(fmi2, 4, fmi2ToFmi3Integer);
    manipulator->override_integer_variable(fmi3, 20, fmi3ToFmi2Integer);
    manipulator->override_boolean_variable(fmi2, 6, fmi2ToFmi3Boolean);
    manipulator->override_boolean_variable(fmi3, 28, fmi3ToFmi2Boolean);
    const auto enumerationValue =
        cosim::variable_value(cosim::enumeration_values{{
            cosim::enumeration_value{2}}});
    manipulator->override_variable(
        fmi2,
        cosim::variable_type::enumeration,
        fmi2EnumerationInput->reference,
        cosim::variable_value(cosim::integer_values{{2}}));
    manipulator->override_variable(
        fmi3,
        cosim::variable_type::enumeration,
        fmi3EnumerationOutput->reference,
        enumerationValue);

    BOOST_CHECK(execution.step() == stepSize);
    BOOST_CHECK(execution.step() == stepSize);

    const auto fmi3IntegerValue = observer->get_variable_value(
        fmi3, cosim::variable_type::integer, 19);
    BOOST_TEST(
        std::get<cosim::integer_values>(fmi3IntegerValue.values())
                .values.at(0) ==
        fmi2ToFmi3Integer);
    const auto fmi2IntegerValue = observer->get_variable_value(
        fmi2, cosim::variable_type::integer, 3);
    BOOST_TEST(
        std::get<cosim::integer_values>(fmi2IntegerValue.values())
                .values.at(0) ==
        fmi3ToFmi2Integer);

    const auto fmi3BooleanValue = observer->get_variable_value(
        fmi3, cosim::variable_type::boolean, 27);
    BOOST_TEST(
        std::get<cosim::boolean_values>(fmi3BooleanValue.values())
                .values.at(0) ==
        fmi2ToFmi3Boolean);
    const auto fmi2BooleanValue = observer->get_variable_value(
        fmi2, cosim::variable_type::boolean, 5);
    BOOST_TEST(
        std::get<cosim::boolean_values>(fmi2BooleanValue.values())
                .values.at(0) ==
        fmi3ToFmi2Boolean);

    const auto fmi3EnumerationValue = observer->get_variable_value(
        fmi3,
        cosim::variable_type::enumeration,
        fmi3EnumerationInput->reference);
    BOOST_TEST(
        std::get<cosim::enumeration_values>(fmi3EnumerationValue.values())
                .values.at(0)
                .value == 2);
    const auto fmi2EnumerationValue = observer->get_variable_value(
        fmi2,
        cosim::variable_type::enumeration,
        fmi2EnumerationInput->reference);
    BOOST_TEST(
        std::get<cosim::enumeration_values>(fmi2EnumerationValue.values())
                .values.at(0)
                .value == 2);
}


BOOST_AUTO_TEST_CASE(fmi3_float32_to_fmi2_real_is_rejected)
{
    const auto* fixtureDirectory =
        std::getenv("FMI3_SYSTEM_STRUCTURE_DIR");
    BOOST_REQUIRE(fixtureDirectory != nullptr);

    auto resolver = cosim::default_model_uri_resolver();
    BOOST_CHECK_EXCEPTION(
        cosim::load_osp_config(
            cosim::filesystem::path(fixtureDirectory) / "invalid" /
                "mixed_float32_to_real.xml",
            *resolver),
        cosim::error,
        [](const cosim::error& error) {
            return error.code() ==
                cosim::make_error_code(cosim::errc::invalid_system_structure);
        });
}


BOOST_AUTO_TEST_CASE(
    fmi2_enumeration_domain_not_accepted_by_fmi3_is_rejected)
{
    const auto* fixtureDirectory =
        std::getenv("FMI3_SYSTEM_STRUCTURE_DIR");
    BOOST_REQUIRE(fixtureDirectory != nullptr);

    auto resolver = cosim::default_model_uri_resolver();
    try {
        cosim::load_osp_config(
            cosim::filesystem::path(fixtureDirectory) / "invalid" /
                "mixed_enumeration_domain.xml",
            *resolver);
        BOOST_FAIL("Expected the incompatible Enumeration route to be rejected");
    } catch (const cosim::error& error) {
        BOOST_TEST(
            error.code() ==
                cosim::make_error_code(cosim::errc::invalid_system_structure),
            "Unexpected error: " << error.what());
    }
}
