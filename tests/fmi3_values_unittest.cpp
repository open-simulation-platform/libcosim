#include "fmi3_test_helpers.hpp"

BOOST_AUTO_TEST_CASE(fmi3_co_simulation)
{
    const auto fmuPath = reference_fmu_directory() / "Feedthrough.fmu";
    require_fixture(fmuPath);

    auto importer = fmi::importer::create();
    auto fmu = std::static_pointer_cast<fmi::v3::fmu>(
        importer->import(fmuPath));
    BOOST_TEST(fmu->fmi_version() == fmi::fmi_version::v3_0);

    const auto description = fmu->model_description();
    BOOST_TEST(description->name == "Feedthrough");
    BOOST_TEST(description->uuid.empty());
    BOOST_TEST(!description->instantiation_token.empty());
    BOOST_TEST(description->capabilities.can_save_state);
    BOOST_TEST(description->capabilities.can_export_state);

    const auto references = variable_references(*description);
    BOOST_TEST(references.count("Float64_continuous_input") == 1U);
    BOOST_TEST(references.count("Int32_input") == 1U);
    BOOST_TEST(references.count("Boolean_input") == 1U);
    BOOST_TEST(references.count("String_input") == 1U);
    BOOST_TEST(references.count("Float32_continuous_input") == 1U);
    BOOST_TEST(references.count("Enumeration_input") == 1U);
    const std::pair<const char*, variable_type> exactTypes[] = {
        {"Float32_continuous_input", variable_type::float32},
        {"Float64_continuous_input", variable_type::real},
        {"Int8_input", variable_type::int8},
        {"UInt8_input", variable_type::uint8},
        {"Int16_input", variable_type::int16},
        {"UInt16_input", variable_type::uint16},
        {"Int32_input", variable_type::integer},
        {"UInt32_input", variable_type::uint32},
        {"Int64_input", variable_type::int64},
        {"UInt64_input", variable_type::uint64},
        {"Boolean_input", variable_type::boolean},
        {"String_input", variable_type::string},
        {"Binary_input", variable_type::binary},
        {"Enumeration_input", variable_type::enumeration},
    };
    for (const auto& [name, type] : exactTypes) {
        const auto* variable = find_model_variable(*description, name);
        BOOST_REQUIRE(variable != nullptr);
        BOOST_TEST(variable->type == type);
    }

    const auto* binaryInput =
        find_model_variable(*description, "Binary_input");
    BOOST_REQUIRE(binaryInput != nullptr);
    BOOST_REQUIRE(binaryInput->start.has_value());
    const auto& binaryStart =
        std::get<binary_value>(*binaryInput->start).data;
    const std::uint8_t expectedBinaryStart[] = {'f', 'o', 'o'};
    BOOST_CHECK_EQUAL_COLLECTIONS(
        binaryStart.begin(),
        binaryStart.end(),
        std::begin(expectedBinaryStart),
        std::end(expectedBinaryStart));

    const auto* enumerationInput =
        find_model_variable(*description, "Enumeration_input");
    BOOST_REQUIRE(enumerationInput != nullptr);
    BOOST_REQUIRE(enumerationInput->declared_type.has_value());
    BOOST_TEST(*enumerationInput->declared_type == "Option");
    BOOST_REQUIRE(enumerationInput->start.has_value());
    BOOST_TEST(
        std::get<enumeration_value>(*enumerationInput->start).value == 1);
    const auto* optionType = find_type_definition(*description, "Option");
    BOOST_REQUIRE(optionType != nullptr);
    BOOST_TEST(optionType->type == variable_type::enumeration);
    BOOST_REQUIRE_EQUAL(optionType->enumeration_items.size(), 2U);
    BOOST_TEST(optionType->enumeration_items[0].name == "Option 1");
    BOOST_TEST(optionType->enumeration_items[0].value == 1);
    BOOST_TEST(
        optionType->enumeration_items[0].description == "First option");
    BOOST_TEST(optionType->enumeration_items[1].name == "Option 2");
    BOOST_TEST(optionType->enumeration_items[1].value == 2);
    BOOST_TEST(
        optionType->enumeration_items[1].description == "Second option");

    auto instance = fmu->instantiate_v3_slave("fmi3-test");
    instance->setup(to_time_point(0.0), to_time_point(2.0), std::nullopt);

    float float32Input = 1.5F;
    double realInput = 1.25;
    std::int8_t int8Input = -8;
    std::uint8_t uint8Input = 8;
    std::int16_t int16Input = -160;
    std::uint16_t uint16Input = 160;
    int integerInput = 17;
    std::uint32_t uint32Input = 32000;
    std::int64_t int64Input = -64000;
    std::uint64_t uint64Input = 64000;
    bool booleanInput = true;
    std::string stringInput = "fmi3";
    binary_value binaryInputValue{{0, 1, 255}};
    enumeration_value enumerationInputValue{2};
    instance->set_float32_variables(
        gsl::make_span(&references.at("Float32_continuous_input"), 1),
        gsl::make_span(&float32Input, 1));
    instance->set_real_variables(
        gsl::make_span(&references.at("Float64_continuous_input"), 1),
        gsl::make_span(&realInput, 1));
    instance->set_int8_variables(
        gsl::make_span(&references.at("Int8_input"), 1),
        gsl::make_span(&int8Input, 1));
    instance->set_uint8_variables(
        gsl::make_span(&references.at("UInt8_input"), 1),
        gsl::make_span(&uint8Input, 1));
    instance->set_int16_variables(
        gsl::make_span(&references.at("Int16_input"), 1),
        gsl::make_span(&int16Input, 1));
    instance->set_uint16_variables(
        gsl::make_span(&references.at("UInt16_input"), 1),
        gsl::make_span(&uint16Input, 1));
    instance->set_integer_variables(
        gsl::make_span(&references.at("Int32_input"), 1),
        gsl::make_span(&integerInput, 1));
    instance->set_uint32_variables(
        gsl::make_span(&references.at("UInt32_input"), 1),
        gsl::make_span(&uint32Input, 1));
    instance->set_int64_variables(
        gsl::make_span(&references.at("Int64_input"), 1),
        gsl::make_span(&int64Input, 1));
    instance->set_uint64_variables(
        gsl::make_span(&references.at("UInt64_input"), 1),
        gsl::make_span(&uint64Input, 1));
    instance->set_boolean_variables(
        gsl::make_span(&references.at("Boolean_input"), 1),
        gsl::make_span(&booleanInput, 1));
    instance->set_string_variables(
        gsl::make_span(&references.at("String_input"), 1),
        gsl::make_span(&stringInput, 1));
    instance->set_binary_variables(
        gsl::make_span(&references.at("Binary_input"), 1),
        gsl::make_span(&binaryInputValue, 1));
    instance->set_enumeration_variables(
        gsl::make_span(&references.at("Enumeration_input"), 1),
        gsl::make_span(&enumerationInputValue, 1));
    enumeration_value invalidEnumerationValue{3};
    BOOST_CHECK_EXCEPTION(
        instance->set_enumeration_variables(
            gsl::make_span(&references.at("Enumeration_input"), 1),
            gsl::make_span(&invalidEnumerationValue, 1)),
        cosim::error,
        error_is(errc::model_error));
    const typed_value_reference genericInputs[] = {
        {variable_type::float32, references.at("Float32_continuous_input")},
        {variable_type::binary, references.at("Binary_input")},
        {variable_type::enumeration, references.at("Enumeration_input")}};
    const variable_value genericInputValues[] = {
        variable_value(float32_values{{float32Input}}),
        variable_value(binary_values{{binaryInputValue}}),
        variable_value(enumeration_values{{enumerationInputValue}})};
    instance->set_variable_values(genericInputs, genericInputValues);
    instance->start_simulation();

    double invalidOutput = 0.0;
    BOOST_CHECK_EXCEPTION(
        instance->set_real_variables(
            gsl::make_span(&references.at("Float64_continuous_output"), 1),
            gsl::make_span(&invalidOutput, 1)),
        cosim::error,
        error_is(errc::model_error));
    instance->set_real_variables(
        gsl::make_span(&references.at("Float64_continuous_input"), 1),
        gsl::make_span(&realInput, 1));
    BOOST_CHECK_EXCEPTION(
        instance->get_real_variables(
            gsl::make_span(&references.at("Float64_continuous_output"), 1),
            gsl::make_span(&invalidOutput, 1)),
        cosim::error,
        error_is(errc::model_error));

    BOOST_CHECK_EXCEPTION(
        instance->do_step(to_time_point(0.0), to_duration(1.0)),
        cosim::error,
        error_is(errc::unsupported_feature));
    BOOST_TEST(instance->do_step_with_info(to_time_point(0.0), to_duration(1.0)).result == step_result::complete);

    float float32Output = 0.0F;
    double realOutput = 0.0;
    std::int8_t int8Output = 0;
    std::uint8_t uint8Output = 0;
    std::int16_t int16Output = 0;
    std::uint16_t uint16Output = 0;
    int integerOutput = 0;
    std::uint32_t uint32Output = 0;
    std::int64_t int64Output = 0;
    std::uint64_t uint64Output = 0;
    bool booleanOutput = false;
    std::string stringOutput;
    binary_value binaryOutput;
    enumeration_value enumerationOutput;
    instance->get_float32_variables(
        gsl::make_span(&references.at("Float32_continuous_output"), 1),
        gsl::make_span(&float32Output, 1));
    instance->get_real_variables(
        gsl::make_span(&references.at("Float64_continuous_output"), 1),
        gsl::make_span(&realOutput, 1));
    instance->get_int8_variables(
        gsl::make_span(&references.at("Int8_output"), 1),
        gsl::make_span(&int8Output, 1));
    instance->get_uint8_variables(
        gsl::make_span(&references.at("UInt8_output"), 1),
        gsl::make_span(&uint8Output, 1));
    instance->get_int16_variables(
        gsl::make_span(&references.at("Int16_output"), 1),
        gsl::make_span(&int16Output, 1));
    instance->get_uint16_variables(
        gsl::make_span(&references.at("UInt16_output"), 1),
        gsl::make_span(&uint16Output, 1));
    instance->get_integer_variables(
        gsl::make_span(&references.at("Int32_output"), 1),
        gsl::make_span(&integerOutput, 1));
    instance->get_uint32_variables(
        gsl::make_span(&references.at("UInt32_output"), 1),
        gsl::make_span(&uint32Output, 1));
    instance->get_int64_variables(
        gsl::make_span(&references.at("Int64_output"), 1),
        gsl::make_span(&int64Output, 1));
    instance->get_uint64_variables(
        gsl::make_span(&references.at("UInt64_output"), 1),
        gsl::make_span(&uint64Output, 1));
    instance->get_boolean_variables(
        gsl::make_span(&references.at("Boolean_output"), 1),
        gsl::make_span(&booleanOutput, 1));
    instance->get_string_variables(
        gsl::make_span(&references.at("String_output"), 1),
        gsl::make_span(&stringOutput, 1));
    instance->get_binary_variables(
        gsl::make_span(&references.at("Binary_output"), 1),
        gsl::make_span(&binaryOutput, 1));
    instance->get_enumeration_variables(
        gsl::make_span(&references.at("Enumeration_output"), 1),
        gsl::make_span(&enumerationOutput, 1));
    BOOST_TEST(float32Output == float32Input);
    BOOST_TEST(realOutput == realInput);
    BOOST_TEST(int8Output == int8Input);
    BOOST_TEST(uint8Output == uint8Input);
    BOOST_TEST(int16Output == int16Input);
    BOOST_TEST(uint16Output == uint16Input);
    BOOST_TEST(integerOutput == integerInput);
    BOOST_TEST(uint32Output == uint32Input);
    BOOST_TEST(int64Output == int64Input);
    BOOST_TEST(uint64Output == uint64Input);
    BOOST_TEST(booleanOutput == booleanInput);
    BOOST_TEST(stringOutput == stringInput);
    BOOST_TEST(binaryOutput == binaryInputValue);
    BOOST_TEST(enumerationOutput == enumerationInputValue);
    const typed_value_reference genericOutputs[] = {
        {variable_type::float32, references.at("Float32_continuous_output")},
        {variable_type::binary, references.at("Binary_output")},
        {variable_type::enumeration, references.at("Enumeration_output")}};
    const auto genericOutputValues =
        instance->get_variable_values(genericOutputs);
    BOOST_REQUIRE_EQUAL(genericOutputValues.size(), 3U);
    BOOST_CHECK(
        genericOutputValues[0] ==
        variable_value(float32_values{{float32Input}}));
    BOOST_CHECK(
        genericOutputValues[1] ==
        variable_value(binary_values{{binaryInputValue}}));
    BOOST_CHECK(
        genericOutputValues[2] ==
        variable_value(enumeration_values{{enumerationInputValue}}));

    const auto state = instance->save_state();
    realInput = 4.5;
    instance->set_real_variables(
        gsl::make_span(&references.at("Float64_continuous_input"), 1),
        gsl::make_span(&realInput, 1));
    BOOST_TEST(instance->do_step_with_info(to_time_point(1.0), to_duration(1.0)).result == step_result::complete);
    instance->restore_state(state);
    realOutput = 0.0;
    instance->get_real_variables(
        gsl::make_span(&references.at("Float64_continuous_output"), 1),
        gsl::make_span(&realOutput, 1));
    BOOST_TEST(realOutput == 1.25);

    const auto exportedState = instance->export_state(state);
    BOOST_TEST(exportedState.get<std::string>("interface") == "co_simulation");
    BOOST_TEST(exportedState.get<double>("stop_time") == 2.0);
    const auto importedState = instance->import_state(exportedState);
    instance->restore_state(importedState);
    instance->release_state(importedState);
    instance->release_state(state);
    BOOST_CHECK_THROW(instance->save_state(state), cosim::error);
    instance->end_simulation();
    instance->get_real_variables(
        gsl::make_span(&references.at("Float64_continuous_output"), 1),
        gsl::make_span(&realOutput, 1));
    BOOST_TEST(realOutput == 1.25);
}


BOOST_AUTO_TEST_CASE(fmi3_exact_value_limits_and_owned_outputs)
{
    const auto path = reference_fmu_directory() / "Feedthrough.fmu";
    require_fixture(path);
    const auto importer = fmi::importer::create();
    const auto fmu = std::dynamic_pointer_cast<fmi::v3::fmu>(importer->import(path));
    BOOST_REQUIRE(fmu);
    const auto instance = fmu->instantiate_v3_slave("exact-limits");
    const auto vr = variable_references(*fmu->model_description());
    const value_reference floatInput = vr.at("Float32_discrete_input");
    const value_reference floatOutput = vr.at("Float32_discrete_output");
    const typed_value_reference inputs[] = {
        {variable_type::int64, vr.at("Int64_input")},
        {variable_type::uint64, vr.at("UInt64_input")},
        {variable_type::string, vr.at("String_input")},
        {variable_type::binary, vr.at("Binary_input")}};
    const typed_value_reference outputs[] = {
        {variable_type::int64, vr.at("Int64_output")},
        {variable_type::uint64, vr.at("UInt64_output")},
        {variable_type::string, vr.at("String_output")},
        {variable_type::binary, vr.at("Binary_output")}};
    const std::vector<variable_value> first{
        variable_value(int64_values{{(std::numeric_limits<std::int64_t>::min)()}}),
        variable_value(uint64_values{{(std::numeric_limits<std::uint64_t>::max)()}}),
        variable_value(string_values{{std::string(127, 'x')}}),
        variable_value(binary_values{{binary_value{{0, 255, 0}}}})};
    const std::vector<variable_value> second{
        variable_value(int64_values{{(std::numeric_limits<std::int64_t>::max)()}}),
        variable_value(uint64_values{{0}}),
        variable_value(string_values{{std::string{}}}),
        variable_value(binary_values{{binary_value{}}})};
    instance->setup(time_point{}, std::nullopt, std::nullopt);
    instance->start_simulation();
    instance->set_variable_values(inputs, gsl::make_span(first));
    const auto infinity = std::numeric_limits<float>::infinity();
    instance->set_float32_variables(gsl::make_span(&floatInput, 1), gsl::make_span(&infinity, 1));
    BOOST_REQUIRE(instance->do_step_with_info(time_point{}, to_duration(1)).result == step_result::complete);
    float floatResult = 0.0F;
    instance->get_float32_variables(gsl::make_span(&floatOutput, 1), gsl::make_span(&floatResult, 1));
    BOOST_CHECK(std::isinf(floatResult) && floatResult > 0.0F);
    const auto copy = instance->get_variable_values(outputs);
    instance->set_variable_values(inputs, gsl::make_span(second));
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    instance->set_float32_variables(gsl::make_span(&floatInput, 1), gsl::make_span(&nan, 1));
    BOOST_REQUIRE(instance->do_step_with_info(to_time_point(1), to_duration(1)).result == step_result::complete);
    instance->get_float32_variables(gsl::make_span(&floatOutput, 1), gsl::make_span(&floatResult, 1));
    BOOST_CHECK(std::isnan(floatResult));
    BOOST_CHECK(instance->get_variable_values(outputs) == second);
    instance->end_simulation();
    BOOST_CHECK(copy == first);
}


// A later invalid Enumeration, type, reference, shape, or lifecycle must not
// write an earlier value; an actual FMU error must still require recovery.
BOOST_AUTO_TEST_CASE(fmi3_generic_batch_prevalidation_and_fmu_failure)
{
    const auto path = fault_fixture("Enumeration.fmu");
    auto importer = fmi::importer::create();
    auto fmu = std::static_pointer_cast<fmi::v3::fmu>(importer->import(path));
    auto instance = fmu->instantiate_v3_slave("fmi3-batch");
    instance->setup(to_time_point(0), to_time_point(1), std::nullopt);
    const typed_value_reference first{variable_type::integer, 1};
    const auto rejected = [&](const char* reason, typed_value_reference second,
                              variable_value badValue, bool inputError) {
        BOOST_TEST_CONTEXT(reason) {
            const typed_value_reference references[] = {first, second};
            const variable_value values[] = {variable_value(integer_values{{1}}), std::move(badValue)};
            if (inputError) {
                BOOST_CHECK_THROW(instance->set_variable_values(references, values), std::invalid_argument);
            } else {
                BOOST_CHECK_THROW(instance->set_variable_values(references, values), cosim::error);
            }
            const auto result = instance->get_variable_values(gsl::make_span(&first, 1));
            BOOST_REQUIRE_EQUAL(result.size(), 1U);
            BOOST_TEST(std::get<integer_values>(result[0].values()).values[0] == 0);
        }
    };
    rejected("undeclared Enumeration item", {variable_type::enumeration, 2}, variable_value(enumeration_values{{enumeration_value{3}}}), false);
    rejected("mismatched value type", {variable_type::enumeration, 2}, variable_value(real_values{{1.0}}), true);
    rejected("unknown reference", {variable_type::integer, 99999}, variable_value(integer_values{{1}}), false);
    rejected("Clock has no generic value representation", {variable_type::clock, 1001}, variable_value(integer_values{{1}}), true);
    rejected("incorrect array shape", {variable_type::enumeration, 3}, variable_value(enumeration_values{{enumeration_value{1}}}, {1}), true);
    rejected("output not settable in initialization", {variable_type::enumeration, 12}, variable_value(enumeration_values{{enumeration_value{1}}}), false);
    const typed_value_reference references[] = {first, {variable_type::enumeration, 2}};
    const variable_value oneValue[] = {variable_value(integer_values{{1}})};
    BOOST_CHECK_THROW(instance->set_variable_values(references, oneValue), std::invalid_argument);
    auto result = instance->get_variable_values(gsl::make_span(&first, 1));
    BOOST_TEST(std::get<integer_values>(result[0].values()).values[0] == 0);
    instance->set_variable_values({}, {});
    const variable_value valid[] = {variable_value(integer_values{{0}}), variable_value(integer_values{{2}})};
    instance->set_variable_values(references, valid);
    result = instance->get_variable_values(gsl::make_span(&references[1], 1));
    BOOST_TEST(std::get<enumeration_values>(result[0].values()).values[0].value == 2);

    // The first assignment makes the fixture's second setter return Error.
    // Error terminates the instance; reads are still available for diagnosis.
    const variable_value fmuFailure[] = {variable_value(integer_values{{3}}), variable_value(integer_values{{1}})};
    BOOST_CHECK_THROW(instance->set_variable_values(references, fmuFailure), cosim::error);
    result = instance->get_variable_values(gsl::make_span(&first, 1));
    BOOST_TEST(std::get<integer_values>(result[0].values()).values[0] == 3);
}

// Test every supported FMI 3 value alternative in one valid mixed batch, then
// ensure invalid later references, types and shapes never write the first value.
BOOST_AUTO_TEST_CASE(fmi3_generic_batch_all_types_prevalidation)
{
    const auto path = reference_fmu_directory() / "Feedthrough.fmu";
    require_fixture(path);
    auto importer = fmi::importer::create();
    auto fmu = std::static_pointer_cast<fmi::v3::fmu>(importer->import(path));
    const auto references = variable_references(*fmu->model_description());
    auto instance = fmu->instantiate_v3_slave("fmi3-batch-all-types");
    instance->setup(to_time_point(0), to_time_point(1), std::nullopt);

    const std::vector<std::pair<typed_value_reference, variable_value>> inputs{
        {{variable_type::real, references.at("Float64_continuous_input")}, variable_value(real_values{{1.25}})},
        {{variable_type::integer, references.at("Int32_input")}, variable_value(integer_values{{17}})},
        {{variable_type::boolean, references.at("Boolean_input")}, variable_value(boolean_values{{true}})},
        {{variable_type::string, references.at("String_input")}, variable_value(string_values{{"batch"}})},
        {{variable_type::enumeration, references.at("Enumeration_input")}, variable_value(enumeration_values{{enumeration_value{2}}})},
        {{variable_type::float32, references.at("Float32_continuous_input")}, variable_value(float32_values{{1.5F}})},
        {{variable_type::int8, references.at("Int8_input")}, variable_value(int8_values{{-8}})},
        {{variable_type::uint8, references.at("UInt8_input")}, variable_value(uint8_values{{8}})},
        {{variable_type::int16, references.at("Int16_input")}, variable_value(int16_values{{-16}})},
        {{variable_type::uint16, references.at("UInt16_input")}, variable_value(uint16_values{{16}})},
        {{variable_type::uint32, references.at("UInt32_input")}, variable_value(uint32_values{{32}})},
        {{variable_type::int64, references.at("Int64_input")}, variable_value(int64_values{{-64}})},
        {{variable_type::uint64, references.at("UInt64_input")}, variable_value(uint64_values{{64}})},
        {{variable_type::binary, references.at("Binary_input")}, variable_value(binary_values{{binary_value{{0, 1, 255}}}})}};
    std::vector<typed_value_reference> batchReferences;
    std::vector<variable_value> batchValues;
    for (const auto& input : inputs) {
        batchReferences.push_back(input.first);
        batchValues.push_back(input.second);
    }
    instance->set_variable_values(batchReferences, batchValues);
    const auto result = instance->get_variable_values(batchReferences);
    BOOST_REQUIRE_EQUAL(result.size(), batchValues.size());
    for (std::size_t i = 0; i < result.size(); ++i) {
        BOOST_TEST_CONTEXT("valid batch value for " << to_text(batchReferences[i].type)) {
            BOOST_CHECK(result[i] == batchValues[i]);
        }
    }

    const auto first = inputs.front().first;
    const variable_value firstValue(real_values{{77.0}});
    const auto rejected = [&](const char* reason, typed_value_reference second,
                              const variable_value& badValue, bool inputError) {
        BOOST_TEST_CONTEXT(reason << " for " << to_text(second.type)) {
            const typed_value_reference batch[] = {first, second};
            const variable_value values[] = {firstValue, badValue};
            if (inputError) {
                BOOST_CHECK_THROW(instance->set_variable_values(batch, values), std::invalid_argument);
            } else {
                BOOST_CHECK_THROW(instance->set_variable_values(batch, values), cosim::error);
            }
            const auto observed = instance->get_variable_values(gsl::make_span(&first, 1));
            BOOST_TEST(std::get<real_values>(observed[0].values()).values[0] == 1.25);
        }
    };
    for (const auto& input : inputs) {
        rejected("unknown typed reference", {input.first.type, 99999}, input.second, false);
        const auto wrong = input.first.type == variable_type::real
            ? variable_value(boolean_values{{true}}) : variable_value(real_values{{1.0}});
        rejected("mismatched value type", input.first, wrong, true);
        const auto array = std::visit([](const auto& source) {
            auto copy = source;
            typename std::decay_t<decltype(copy)>::value_type element = copy.values[0];
            copy.values.push_back(element);
            return variable_value(std::move(copy), {2});
        }, input.second.values());
        rejected("incorrect scalar shape", input.first, array, true);
    }
}

// A Configuration Mode resize must determine the shape of subsequent writes.
// A rejected batch must not apply an earlier resize or scalar write.
BOOST_AUTO_TEST_CASE(fmi3_generic_batches_honor_structural_shapes_and_lifecycle)
{
    const auto path = fault_fixture("Fmi3Derivatives.fmu");
    auto importer = fmi::importer::create();
    auto fmu = std::static_pointer_cast<fmi::v3::fmu>(importer->import(path));
    auto instance = fmu->instantiate_v3_slave("fmi3-batch-shapes");
    const typed_value_reference size{variable_type::uint64, 4};
    const typed_value_reference array{variable_type::real, 5};
    const typed_value_reference scalar{variable_type::real, 1};
    instance->enter_configuration_mode();
    const variable_value three{uint64_values{{3}}};
    instance->set_variable_values(gsl::make_span(&size, 1), gsl::make_span(&three, 1));

    // Inputs cannot be set in Configuration Mode, so the resize preceding
    // this input must remain unapplied when the complete batch is rejected.
    const typed_value_reference configurationReferences[] = {size, array};
    const variable_value invalidConfiguration[] = {
        variable_value(uint64_values{{2}}), variable_value(real_values{{4.0, 5.0}}, {2})};
    BOOST_CHECK_THROW(
        instance->set_variable_values(configurationReferences, invalidConfiguration),
        cosim::error);
    BOOST_CHECK_EXCEPTION(instance->get_variable_values(gsl::make_span(&size, 1)),
        cosim::error, error_is(errc::model_error));
    instance->exit_configuration_mode();
    auto result = instance->get_variable_values(gsl::make_span(&size, 1));
    BOOST_TEST(std::get<uint64_values>(result[0].values()).values[0] == 3U);

    const variable_value resizedArray{real_values{{1.0, 2.0, 3.0}}, {3}};
    instance->set_variable_values(gsl::make_span(&array, 1), gsl::make_span(&resizedArray, 1));
    result = instance->get_variable_values(gsl::make_span(&array, 1));
    BOOST_TEST(result[0].size() == 3U);

    const typed_value_reference inputReferences[] = {scalar, array};
    const variable_value invalidShape[] = {
        variable_value(real_values{{7.0}}), variable_value(real_values{{4.0, 5.0}}, {2})};
    BOOST_CHECK_THROW(
        instance->set_variable_values(inputReferences, invalidShape),
        std::invalid_argument);
    result = instance->get_variable_values(inputReferences);
    BOOST_TEST(std::get<real_values>(result[0].values()).values[0] == 0.0);
    BOOST_TEST(std::get<real_values>(result[1].values()).values[0] == 1.0);
}

BOOST_AUTO_TEST_CASE(fmi3_runtime_reconfiguration_requires_tunable_structural_parameters)
{
    const auto path = reference_fmu_directory() / "FixedArrayStateSpace.fmu";
    require_fixture(path);
    utility::temp_dir unpacked;
    utility::zip::archive(path).extract_all(unpacked.path());
    rewrite_model_description(unpacked.path(),
        "description=\"Number of outputs\" causality=\"structuralParameter\" variability=\"tunable\"",
        "description=\"Number of outputs\" causality=\"structuralParameter\" variability=\"fixed\"");

    auto importer = fmi::importer::create();
    auto fmu = std::static_pointer_cast<fmi::v3::fmu>(
        importer->import_unpacked(unpacked.path()));
    auto instance = fmu->instantiate_v3_slave("reconfiguration");
    const value_reference inputSize[] = {1};
    const value_reference outputSize[] = {3};
    const std::uint64_t two[] = {2};
    instance->enter_configuration_mode();
    instance->set_uint64_variables(outputSize, two);
    instance->exit_configuration_mode();
    instance->setup(time_point{}, std::nullopt, std::nullopt);
    instance->start_simulation();

    instance->enter_event_mode();
    BOOST_CHECK_THROW(instance->enter_configuration_mode(), cosim::error);
    instance->handle_events();
    instance->enter_configuration_mode();
    const value_reference mixedReferences[] = {1, 3};
    const std::uint64_t mixedSizes[] = {2, 1};
    BOOST_CHECK_EXCEPTION(instance->set_uint64_variables(mixedReferences, mixedSizes),
        cosim::error, error_is(errc::model_error));
    const typed_value_reference wholeReferences[] = {
        {variable_type::uint64, 1}, {variable_type::uint64, 3}};
    const variable_value wholeSizes[] = {
        variable_value(uint64_values{{2}}), variable_value(uint64_values{{1}})};
    BOOST_CHECK_EXCEPTION(instance->set_variable_values(wholeReferences, wholeSizes),
        cosim::error, error_is(errc::model_error));
    BOOST_REQUIRE_EQUAL(instance->resolved_shape(variable_type::real, 9).at(0), 3U);
    instance->set_uint64_variables(inputSize, two);
    instance->exit_configuration_mode();
    BOOST_REQUIRE_EQUAL(instance->resolved_shape(variable_type::real, 9).at(0), 2U);
    BOOST_REQUIRE_EQUAL(instance->resolved_shape(variable_type::real, 10).at(0), 2U);
    const value_reference input[] = {9};
    const double wrongCount[] = {1.0, 2.0, 3.0};
    const double resized[] = {1.0, 2.0};
    BOOST_CHECK_THROW(instance->set_real_variables(input, wrongCount), std::invalid_argument);
    instance->set_real_variables(input, resized);
    instance->end_simulation();

    const auto fixedOnlyPath = fault_fixture("Fmi3Derivatives.fmu");
    utility::temp_dir fixedOnlyUnpacked;
    utility::zip::archive(fixedOnlyPath).extract_all(fixedOnlyUnpacked.path());
    rewrite_model_description(fixedOnlyUnpacked.path(),
        "causality=\"structuralParameter\" variability=\"tunable\"",
        "causality=\"structuralParameter\" variability=\"fixed\"");
    auto fixedOnlyFmu = std::static_pointer_cast<fmi::v3::fmu>(
        importer->import_unpacked(fixedOnlyUnpacked.path()));
    auto noResize = fixedOnlyFmu->instantiate_v3_slave("no-reconfiguration");
    noResize->setup(time_point{}, std::nullopt, std::nullopt);
    noResize->start_simulation();
    BOOST_CHECK_EXCEPTION(noResize->enter_configuration_mode(), cosim::error,
        error_is(errc::model_error));
    BOOST_CHECK(noResize->do_step_with_info(time_point{}, to_duration(0.01)).result ==
        step_result::complete);
}

BOOST_AUTO_TEST_CASE(fmi3_enumeration_fixture)
{
    const auto fmuPath = fault_fixture("Enumeration.fmu");

    auto importer = fmi::importer::create();
    auto fmu = std::static_pointer_cast<fmi::v3::fmu>(
        importer->import(fmuPath));
    const auto description = fmu->model_description();
    BOOST_TEST(description->name == "Enumeration");
    BOOST_REQUIRE_EQUAL(fmu->clock_descriptions().size(), 2U);

    const auto* optionType =
        find_type_definition(*description, "SignedOption");
    BOOST_REQUIRE(optionType != nullptr);
    BOOST_TEST(optionType->type == variable_type::enumeration);
    BOOST_REQUIRE(optionType->quantity.has_value());
    BOOST_TEST(*optionType->quantity == "mode");
    BOOST_REQUIRE_EQUAL(optionType->enumeration_items.size(), 4U);
    BOOST_TEST(optionType->enumeration_items[0].name == "Negative");
    BOOST_TEST(optionType->enumeration_items[0].value == -1);
    BOOST_TEST(optionType->enumeration_items[0].description == "Negative option");
    BOOST_TEST(optionType->enumeration_items[1].value == 1);
    BOOST_TEST(optionType->enumeration_items[2].value == 2);
    BOOST_TEST(optionType->enumeration_items[3].value == 4);

    const auto references = variable_references(*description);
    const auto* scalarInput =
        find_model_variable(*description, "scalarInput");
    const auto* arrayInput =
        find_model_variable(*description, "arrayInput");
    const auto* emptyOutputVariable =
        find_model_variable(*description, "emptyOutput");
    BOOST_REQUIRE(scalarInput != nullptr);
    BOOST_REQUIRE(arrayInput != nullptr);
    BOOST_REQUIRE(emptyOutputVariable != nullptr);
    BOOST_TEST(scalarInput->type == variable_type::enumeration);
    BOOST_REQUIRE(scalarInput->declared_type.has_value());
    BOOST_TEST(*scalarInput->declared_type == "SignedOption");
    BOOST_REQUIRE(scalarInput->start.has_value());
    BOOST_TEST(std::get<enumeration_value>(*scalarInput->start).value == 1);
    BOOST_REQUIRE_EQUAL(arrayInput->dimensions.size(), 1U);
    BOOST_TEST(
        std::get<fixed_dimension>(arrayInput->dimensions.front()).size == 2U);
    BOOST_REQUIRE_EQUAL(emptyOutputVariable->dimensions.size(), 1U);
    BOOST_TEST(
        std::get<fixed_dimension>(emptyOutputVariable->dimensions.front()).size == 0U);

    auto instance = fmu->instantiate_v3_slave("enumeration-cosim");
    instance->setup(to_time_point(0.0), to_time_point(1.0), std::nullopt);

    const std::array<value_reference, 1> scalarReference = {
        references.at("scalarInput")};
    const std::array<value_reference, 1> arrayReference = {
        references.at("arrayInput")};
    std::array<enumeration_value, 1> scalarValue = {enumeration_value{-1}};
    std::array<enumeration_value, 2> arrayValue = {
        enumeration_value{4}, enumeration_value{-1}};
    instance->set_enumeration_variables(
        gsl::make_span(scalarReference),
        gsl::make_span(scalarValue));
    instance->set_enumeration_variables(
        gsl::make_span(arrayReference),
        gsl::make_span(arrayValue));
    const typed_value_reference scalarGeneric{
        variable_type::enumeration, scalarReference[0]};
    const variable_value integerScalar(integer_values{{-1}});
    instance->set_variable_values(
        gsl::make_span(&scalarGeneric, 1), gsl::make_span(&integerScalar, 1));
    const typed_value_reference arrayGeneric{
        variable_type::enumeration, arrayReference[0]};
    const variable_value integerArray(integer_values{{4, -1}}, {2});
    BOOST_CHECK_THROW(instance->set_variable_values(
        gsl::make_span(&arrayGeneric, 1), gsl::make_span(&integerArray, 1)),
        std::invalid_argument);
    enumeration_value invalidValue{3};
    BOOST_CHECK_EXCEPTION(
        instance->set_enumeration_variables(
            scalarReference,
            gsl::make_span(&invalidValue, 1)),
        cosim::error,
        error_is(errc::model_error));

    instance->start_simulation();
    instance->handle_events();
    BOOST_TEST(
        instance->do_step_with_info(to_time_point(0.0), to_duration(0.1)).result ==
        step_result::complete);

    std::array<enumeration_value, 1> scalarOutput = {};
    std::array<enumeration_value, 2> arrayOutput = {};
    std::array<enumeration_value, 0> emptyOutput = {};
    const std::array<value_reference, 1> scalarOutputReference = {
        references.at("scalarOutput")};
    const std::array<value_reference, 1> arrayOutputReference = {
        references.at("arrayOutput")};
    const std::array<value_reference, 1> emptyOutputReference = {
        references.at("emptyOutput")};
    instance->get_enumeration_variables(
        gsl::make_span(scalarOutputReference),
        gsl::make_span(scalarOutput));
    instance->get_enumeration_variables(
        gsl::make_span(arrayOutputReference),
        gsl::make_span(arrayOutput));
    instance->get_enumeration_variables(
        gsl::make_span(emptyOutputReference),
        gsl::make_span(emptyOutput));
    BOOST_TEST(scalarOutput[0].value == -1);
    BOOST_TEST(arrayOutput[0].value == 4);
    BOOST_TEST(arrayOutput[1].value == -1);

    const typed_value_reference genericReferences[] = {
        {variable_type::enumeration, references.at("scalarOutput")},
        {variable_type::enumeration, references.at("arrayOutput")},
        {variable_type::enumeration, references.at("emptyOutput")}};
    const auto genericValues = instance->get_variable_values(genericReferences);
    BOOST_REQUIRE_EQUAL(genericValues.size(), 3U);
    BOOST_TEST(genericValues[0].is_scalar());
    BOOST_TEST(
        std::get<enumeration_values>(genericValues[0].values()).values[0].value ==
        -1);
    BOOST_TEST(genericValues[1].dimensions() == std::vector<std::uint64_t>{2});
    BOOST_TEST(
        std::get<enumeration_values>(genericValues[1].values()).values[0].value ==
        4);
    BOOST_TEST(
        std::get<enumeration_values>(genericValues[1].values()).values[1].value ==
        -1);
    BOOST_TEST(genericValues[2].dimensions() == std::vector<std::uint64_t>{0});
    BOOST_TEST(
        std::get<enumeration_values>(genericValues[2].values()).values.empty());

    std::array<enumeration_value, 1> wrongCount = {};
    BOOST_CHECK_THROW(
        instance->set_enumeration_variables(
            gsl::make_span(arrayReference),
            gsl::make_span(wrongCount)),
        std::invalid_argument);
    std::array<enumeration_value, 1> unchanged = {enumeration_value{4}};
    BOOST_CHECK_THROW(
        instance->get_enumeration_variables(
            gsl::make_span(arrayOutputReference),
            gsl::make_span(unchanged)),
        std::invalid_argument);
    BOOST_TEST(unchanged[0].value == 4);

    const std::array<value_reference, 1> statusReference = {
        references.at("status")};
    std::int32_t status = 2;
    instance->set_integer_variables(
        gsl::make_span(statusReference),
        gsl::make_span(&status, 1));
    std::array<enumeration_value, 1> discarded = {enumeration_value{4}};
    BOOST_CHECK_THROW(
        instance->get_enumeration_variables(
            gsl::make_span(scalarOutputReference),
            gsl::make_span(discarded)),
        cosim::error);
    BOOST_TEST(discarded[0].value == 4);

    instance->end_simulation();
}


BOOST_AUTO_TEST_CASE(fmi3_string_binary_enumeration_arrays)
{
    const auto fmuPath =
        array_fixture_directory() / "ArrayFeedthrough.fmu";
    require_fixture(fmuPath);

    auto importer = fmi::importer::create();
    auto fmu = std::static_pointer_cast<fmi::v3::fmu>(
        importer->import(fmuPath));
    const auto description = fmu->model_description();
    const std::vector<std::uint64_t> arrayShape{2};

    const auto* stringVariable =
        find_model_variable(*description, "String_array");
    const auto* binaryVariable =
        find_model_variable(*description, "Binary_array");
    const auto* enumerationVariable =
        find_model_variable(*description, "Enumeration_array");
    BOOST_REQUIRE(stringVariable != nullptr);
    BOOST_REQUIRE(binaryVariable != nullptr);
    BOOST_REQUIRE(enumerationVariable != nullptr);
    BOOST_TEST(stringVariable->type == variable_type::string);
    BOOST_TEST(binaryVariable->type == variable_type::binary);
    BOOST_TEST(enumerationVariable->type == variable_type::enumeration);
    BOOST_REQUIRE_EQUAL(stringVariable->dimensions.size(), 1U);
    BOOST_REQUIRE_EQUAL(binaryVariable->dimensions.size(), 1U);
    BOOST_REQUIRE_EQUAL(enumerationVariable->dimensions.size(), 1U);
    BOOST_TEST(
        std::get<fixed_dimension>(stringVariable->dimensions[0]).size == 2U);
    BOOST_TEST(
        std::get<fixed_dimension>(binaryVariable->dimensions[0]).size == 2U);
    BOOST_TEST(
        std::get<fixed_dimension>(enumerationVariable->dimensions[0]).size == 2U);

    BOOST_REQUIRE(stringVariable->array_start.has_value());
    BOOST_REQUIRE(binaryVariable->array_start.has_value());
    BOOST_REQUIRE(enumerationVariable->array_start.has_value());
    const std::vector<std::string> expectedStringStart{"first", "second"};
    const std::vector<binary_value> expectedBinaryStart{
        binary_value{{0x01U, 0x02U}}, binary_value{{0xa0U, 0xffU}}};
    const std::vector<enumeration_value> expectedEnumerationStart{
        enumeration_value{1}, enumeration_value{2}};
    const auto& stringStart =
        std::get<string_values>(stringVariable->array_start->values()).values;
    const auto& binaryStart =
        std::get<binary_values>(binaryVariable->array_start->values()).values;
    const auto& enumerationStart =
        std::get<enumeration_values>(
            enumerationVariable->array_start->values())
            .values;
    BOOST_CHECK_EQUAL_COLLECTIONS(
        stringStart.begin(),
        stringStart.end(),
        expectedStringStart.begin(),
        expectedStringStart.end());
    BOOST_CHECK_EQUAL_COLLECTIONS(
        binaryStart.begin(),
        binaryStart.end(),
        expectedBinaryStart.begin(),
        expectedBinaryStart.end());
    BOOST_CHECK_EQUAL_COLLECTIONS(
        enumerationStart.begin(),
        enumerationStart.end(),
        expectedEnumerationStart.begin(),
        expectedEnumerationStart.end());

    auto instance = fmu->instantiate_v3_slave("fmi3-array-values");
    instance->setup(to_time_point(0.0), to_time_point(1.0), std::nullopt);
    const std::vector<typed_value_reference> references{
        {variable_type::string, stringVariable->reference},
        {variable_type::binary, binaryVariable->reference},
        {variable_type::enumeration, enumerationVariable->reference}};
    const std::vector<variable_value> values{
        variable_value(string_values{{"updated", "strings"}}, arrayShape),
        variable_value(
            binary_values{
                {binary_value{{0x10U, 0x20U}}, binary_value{{0x30U}}}},
            arrayShape),
        variable_value(
            enumeration_values{
                {enumeration_value{2}, enumeration_value{1}}},
            arrayShape)};
    instance->set_variable_values(
        gsl::make_span(references.data(), references.size()),
        gsl::make_span(values.data(), values.size()));
    instance->start_simulation();

    const auto result = instance->get_variable_values(
        gsl::make_span(references.data(), references.size()));
    BOOST_REQUIRE_EQUAL(result.size(), references.size());
    const std::vector<std::string> expectedStrings{"updated", "strings"};
    const std::vector<binary_value> expectedBinaries{
        binary_value{{0x10U, 0x20U}}, binary_value{{0x30U}}};
    const std::vector<enumeration_value> expectedEnumerations{
        enumeration_value{2}, enumeration_value{1}};
    const auto& strings =
        std::get<string_values>(result[0].values()).values;
    const auto& binaries =
        std::get<binary_values>(result[1].values()).values;
    const auto& enumerations =
        std::get<enumeration_values>(result[2].values()).values;
    BOOST_CHECK_EQUAL_COLLECTIONS(
        strings.begin(),
        strings.end(),
        expectedStrings.begin(),
        expectedStrings.end());
    BOOST_CHECK_EQUAL_COLLECTIONS(
        binaries.begin(),
        binaries.end(),
        expectedBinaries.begin(),
        expectedBinaries.end());
    BOOST_CHECK_EQUAL_COLLECTIONS(
        enumerations.begin(),
        enumerations.end(),
        expectedEnumerations.begin(),
        expectedEnumerations.end());

    const variable_value invalidEnumeration(
        enumeration_values{
            {enumeration_value{2}, enumeration_value{3}}},
        arrayShape);
    BOOST_CHECK_EXCEPTION(
        instance->set_variable_values(
            gsl::make_span(&references[2], 1),
            gsl::make_span(&invalidEnumeration, 1)),
        cosim::error,
        error_is(errc::model_error));
    const auto unchanged = instance->get_variable_values(
        gsl::make_span(&references[2], 1));
    const auto& unchangedEnumerations =
        std::get<enumeration_values>(unchanged[0].values()).values;
    BOOST_CHECK_EQUAL_COLLECTIONS(
        unchangedEnumerations.begin(),
        unchangedEnumerations.end(),
        expectedEnumerations.begin(),
        expectedEnumerations.end());
    instance->end_simulation();
}


BOOST_AUTO_TEST_CASE(fmi3_enumeration_status_contract)
{
    const auto fmuPath = fault_fixture("Enumeration.fmu");

    auto importer = fmi::importer::create();
    auto fmu = std::static_pointer_cast<fmi::v3::fmu>(
        importer->import(fmuPath));
    const auto references = variable_references(*fmu->model_description());
    const auto makeInstance =
        [&fmu](const std::string& name) {
            auto instance = fmu->instantiate_v3_slave(name);
            instance->setup(
                to_time_point(0.0), to_time_point(1.0), std::nullopt);
            instance->start_simulation();
            return instance;
        };
    const auto setStatus =
        [&references](fmi::v3::slave_instance& instance, int status) {
            instance.set_integer_variables(
                gsl::make_span(&references.at("status"), 1),
                gsl::make_span(&status, 1));
        };
    const value_reference outputReference = references.at("scalarOutput");

    {
        auto instance = makeInstance("fmi3-enumeration-warning");
        setStatus(*instance, 1);
        enumeration_value value{99};
        instance->get_enumeration_variables(
            gsl::make_span(&outputReference, 1),
            gsl::make_span(&value, 1));
        BOOST_TEST(value.value == 1);
        instance->end_simulation();
    }

    for (const int status : std::array<int, 2>{2, 3}) {
        auto instance = makeInstance(
            "fmi3-enumeration-failure-" + std::to_string(status));
        setStatus(*instance, status);
        enumeration_value unchanged{99};
        BOOST_CHECK_EXCEPTION(
            instance->get_enumeration_variables(
                gsl::make_span(&outputReference, 1),
                gsl::make_span(&unchanged, 1)),
            cosim::error,
            error_is(errc::model_error));
        BOOST_TEST(unchanged.value == 99);
    }

    auto fatalImporter = fmi::importer::create();
    auto fatalFmu = std::static_pointer_cast<fmi::v3::fmu>(
        fatalImporter->import(fmuPath));
    auto fatalInstance = fatalFmu->instantiate_v3_slave(
        "fmi3-enumeration-fatal");
    fatalInstance->setup(
        to_time_point(0.0), to_time_point(1.0), std::nullopt);
    fatalInstance->start_simulation();
    const auto fatalReferences = variable_references(*fatalFmu->model_description());
    const value_reference fatalStatusReference = fatalReferences.at("status");
    int fatalStatus = 4;
    fatalInstance->set_integer_variables(
        gsl::make_span(&fatalStatusReference, 1),
        gsl::make_span(&fatalStatus, 1));
    enumeration_value unchanged{99};
    BOOST_CHECK_EXCEPTION(
        fatalInstance->get_enumeration_variables(
            gsl::make_span(&outputReference, 1),
            gsl::make_span(&unchanged, 1)),
        cosim::error,
        error_is(errc::model_error));
    BOOST_TEST(unchanged.value == 99);
    BOOST_CHECK_THROW(
        fatalInstance->get_enumeration_variables(
            gsl::make_span(&outputReference, 1),
            gsl::make_span(&unchanged, 1)),
        cosim::error);
}


BOOST_AUTO_TEST_CASE(fmi3_intermediate_update)
{
    const auto fmuPath = reference_fmu_directory() / "BouncingBall.fmu";
    require_fixture(fmuPath);

    auto importer = fmi::importer::create();
    auto fmu = std::static_pointer_cast<fmi::v3::fmu>(importer->import(fmuPath));
    const auto references = variable_references(*fmu->model_description());
    const auto height = references.at("h");
    const auto velocity = references.at("v");

    fmi::v3::co_simulation_callbacks duplicateCallbacks;
    duplicateCallbacks.required_intermediate_variables = {height, height};
    duplicateCallbacks.intermediate_update =
        [](fmi::v3::slave_instance&, fmi::v3::intermediate_update_info&) { };
    BOOST_CHECK_EXCEPTION(
        fmu->instantiate_v3_slave(
            "fmi3-intermediate-duplicate", std::move(duplicateCallbacks)),
        cosim::error,
        error_is(errc::unsupported_feature));

    std::atomic<int> callbackCount{0};
    std::atomic<double> observedHeight{0.0};
    std::atomic<bool> wholeValueAccessed{false};
    fmi::v3::co_simulation_callbacks callbacks;
    callbacks.early_return_allowed = true;
    callbacks.intermediate_update =
        [height, velocity, &callbackCount, &observedHeight, &wholeValueAccessed](
            fmi::v3::slave_instance& instance,
            fmi::v3::intermediate_update_info& info) {
            BOOST_TEST(info.intermediate_variable_get_allowed);
            BOOST_TEST(info.intermediate_step_finished);
            const typed_value_reference variables[] = {
                {variable_type::real, height},
                {variable_type::real, velocity}};
            const auto values =
                instance.get_variable_values(gsl::make_span(variables));
            BOOST_REQUIRE_EQUAL(values.size(), 2U);
            BOOST_TEST(values[0].dimensions().empty());
            BOOST_TEST(values[1].dimensions().empty());
            observedHeight =
                std::get<real_values>(values[0].values()).values.at(0);
            wholeValueAccessed = true;
            ++callbackCount;
        };

    auto instance =
        fmu->instantiate_v3_slave("fmi3-intermediate", std::move(callbacks));
    instance->setup(to_time_point(0.0), to_time_point(1.0), std::nullopt);
    instance->start_simulation();
    const auto result =
        instance->do_step_with_info(to_time_point(0.0), to_duration(0.01));
    BOOST_TEST(result.result == step_result::complete);
    BOOST_TEST(callbackCount > 0);
    BOOST_TEST(std::isfinite(observedHeight.load()));
    BOOST_TEST(wholeValueAccessed);

    const auto eventResult =
        instance->do_step_with_info(to_time_point(0.01), to_duration(0.99));
    BOOST_TEST(eventResult.result == step_result::complete);
    BOOST_TEST(eventResult.early_return);
    BOOST_TEST(eventResult.event_handling_needed);
    BOOST_CHECK(eventResult.last_successful_time > to_time_point(0.01));
    BOOST_CHECK(eventResult.last_successful_time < to_time_point(1.0));
    instance->handle_events();
    instance->end_simulation();
}


BOOST_AUTO_TEST_CASE(fmi3_intermediate_update_float32)
{
    const auto fmuPath =
        reference_fmu_directory() / "IntermediateFeedthrough.fmu";
    require_fixture(fmuPath);

    auto importer = fmi::importer::create();
    auto fmu = std::static_pointer_cast<fmi::v3::fmu>(importer->import(fmuPath));
    const auto references = variable_references(*fmu->model_description());
    const auto input = references.at("Float32_continuous_input");
    const auto output = references.at("Float32_continuous_output");

    std::atomic<int> callbackCount{0};
    std::atomic<float> observedOutput{0.0F};
    std::atomic<bool> disallowedSetRejected{false};
    fmi::v3::co_simulation_callbacks callbacks;
    callbacks.required_intermediate_variables = {input, output};
    callbacks.intermediate_update =
        [input, output, &callbackCount, &observedOutput,
            &disallowedSetRejected](
            fmi::v3::slave_instance& instance,
            fmi::v3::intermediate_update_info& info) {
            BOOST_TEST(info.intermediate_variable_get_allowed);
            BOOST_TEST(!info.intermediate_variable_set_requested);
            float value = 0.0F;
            instance.get_float32_variables(
                gsl::make_span(&output, 1),
                gsl::make_span(&value, 1));
            observedOutput = value;
            try {
                instance.set_float32_variables(
                    gsl::make_span(&input, 1),
                    gsl::make_span(&value, 1));
            } catch (const cosim::error& error) {
                disallowedSetRejected =
                    error.code() == make_error_code(errc::model_error);
            }
            ++callbackCount;
        };

    auto instance =
        fmu->instantiate_v3_slave("fmi3-intermediate-float32", std::move(callbacks));
    instance->setup(to_time_point(0.0), to_time_point(1.0), std::nullopt);
    const float inputValue = 2.5F;
    instance->set_float32_variables(
        gsl::make_span(&input, 1),
        gsl::make_span(&inputValue, 1));
    instance->start_simulation();
    const auto result =
        instance->do_step_with_info(to_time_point(0.0), to_duration(0.1));
    BOOST_TEST(result.result == step_result::complete);
    BOOST_TEST(callbackCount > 0);
    BOOST_TEST(observedOutput == inputValue);
    BOOST_TEST(disallowedSetRejected);
    instance->end_simulation();
}

BOOST_AUTO_TEST_CASE(fmi3_intermediate_callback_exception_is_consumed)
{
    const auto fmuPath =
        reference_fmu_directory() / "IntermediateFeedthrough.fmu";
    require_fixture(fmuPath);

    auto importer = fmi::importer::create();
    auto fmu = std::static_pointer_cast<fmi::v3::fmu>(importer->import(fmuPath));
    fmi::v3::co_simulation_callbacks callbacks;
    callbacks.intermediate_update =
        [](fmi::v3::slave_instance&, fmi::v3::intermediate_update_info&) {
            throw std::runtime_error("intermediate callback failure");
        };
    auto instance = fmu->instantiate_v3_slave(
        "fmi3-intermediate-callback-failure", std::move(callbacks));
    instance->setup(to_time_point(0.0), to_time_point(1.0), std::nullopt);
    instance->start_simulation();
    BOOST_CHECK_EXCEPTION(
        instance->do_step_with_info(to_time_point(0.0), to_duration(0.1)),
        std::runtime_error,
        [](const std::runtime_error& error) {
            return std::string(error.what()) == "intermediate callback failure";
        });
    BOOST_CHECK_THROW(
        instance->do_step_with_info(to_time_point(0.0), to_duration(0.1)),
        cosim::error);
}


BOOST_AUTO_TEST_CASE(fmi3_debug_logging_controls_declared_categories)
{
    const auto* path = std::getenv("FMI3_LOGGING_FIXTURE");
    BOOST_REQUIRE(path);
    auto importer = fmi::importer::create();
    auto model = std::dynamic_pointer_cast<fmi::v3::fmu>(
        importer->import(path));
    BOOST_REQUIRE(model);
    auto instance = model->instantiate_v3_slave("log_fixture");
    const std::string_view unknown[] = {"notDeclared"};
    BOOST_CHECK_THROW(instance->set_debug_logging(true, unknown), std::invalid_argument);

    // Pass a view that is not NUL-terminated at its end.
    const std::string_view onlyA[] = {std::string_view("logFixtureA-suffix", sizeof("logFixtureA") - 1)};
    instance->set_debug_logging(false);
    instance->set_debug_logging(true, onlyA);
    auto time = to_time_point(0.0);
    const auto step = to_duration(0.1);
    instance->setup(time, std::nullopt, std::nullopt);
    instance->start_simulation();
    const auto step_and_read_log = [&] {
        warning_capture capture(log::trace);
        BOOST_CHECK(instance->do_step_with_info(time, step).result == step_result::complete);
        time += step;
        return capture.messages();
    };

    const auto onlyAOutput = step_and_read_log();
    instance->set_debug_logging(false, onlyA);
    const auto noOutput = step_and_read_log();
    instance->set_debug_logging(true);
    const auto bothOutput = step_and_read_log();
#ifndef LIBCOSIM_NO_FMI_LOGGING
    BOOST_CHECK(onlyAOutput.find("instance=log_fixture, category=logFixtureA") != std::string::npos);
    BOOST_CHECK(onlyAOutput.find("category=logFixtureB") == std::string::npos);
    BOOST_CHECK(noOutput.find("instance=log_fixture") == std::string::npos);
    BOOST_CHECK(bothOutput.find("category=logFixtureA") != std::string::npos);
    BOOST_CHECK(bothOutput.find("category=logFixtureB") != std::string::npos);
#endif
    instance->end_simulation();
}

BOOST_AUTO_TEST_CASE(fmi3_derivatives)
{
    auto importer = fmi::importer::create();

    const auto vanDerPolPath =
        reference_fmu_directory() / "VanDerPol.fmu";
    require_fixture(vanDerPolPath);
    auto vanDerPol = std::static_pointer_cast<fmi::v3::fmu>(
        importer->import(vanDerPolPath));
    auto derivativeInstance =
        vanDerPol->instantiate_v3_slave("fmi3-partial-derivatives");
    BOOST_TEST(derivativeInstance->provides_directional_derivatives());
    BOOST_TEST(derivativeInstance->provides_adjoint_derivatives());
    derivativeInstance->setup(
        to_time_point(0.0), to_time_point(1.0), std::nullopt);

    const value_reference unknowns[] = {2, 4};
    const value_reference knowns[] = {1, 3, 5};
    const double directionalSeed[] = {1.0, 1.0, 1.0};
    double directionalSensitivity[] = {0.0, 0.0};
    derivativeInstance->get_directional_derivative(
        unknowns,
        knowns,
        directionalSeed,
        directionalSensitivity);
    BOOST_TEST(
        directionalSensitivity[0] == 1.0,
        boost::test_tools::tolerance(1e-12));
    BOOST_TEST(
        directionalSensitivity[1] == -4.0,
        boost::test_tools::tolerance(1e-12));

    const double adjointSeed[] = {1.0, 1.0};
    double adjointSensitivity[] = {0.0, 0.0, 0.0};
    derivativeInstance->get_adjoint_derivative(
        unknowns, knowns, adjointSeed, adjointSensitivity);
    const double expectedAdjoint[] = {-1.0, -2.0, 0.0};
    BOOST_CHECK_CLOSE_FRACTION(
        adjointSensitivity[0], expectedAdjoint[0], 1e-12);
    BOOST_CHECK_CLOSE_FRACTION(
        adjointSensitivity[1], expectedAdjoint[1], 1e-12);
    BOOST_TEST(adjointSensitivity[2] == expectedAdjoint[2]);

    double wrongSensitivity = 0.0;
    BOOST_CHECK_THROW(
        derivativeInstance->get_directional_derivative(
            unknowns,
            knowns,
            directionalSeed,
            gsl::make_span(&wrongSensitivity, 1)),
        std::invalid_argument);
    derivativeInstance->start_simulation();

    const value_reference stepKnowns[] = {1, 3};
    const double stepDirectionalSeed[] = {2.0, -1.0};
    double stepDirectionalSensitivity[] = {0.0, 0.0};
    derivativeInstance->get_directional_derivative(
        unknowns,
        stepKnowns,
        stepDirectionalSeed,
        stepDirectionalSensitivity);
    BOOST_TEST(stepDirectionalSensitivity[0] == -1.0);
    BOOST_TEST(stepDirectionalSensitivity[1] == 1.0);

    const double stepAdjointSeed[] = {3.0, -2.0};
    double stepAdjointSensitivity[] = {0.0, 0.0};
    derivativeInstance->get_adjoint_derivative(
        unknowns,
        stepKnowns,
        stepAdjointSeed,
        stepAdjointSensitivity);
    BOOST_TEST(stepAdjointSensitivity[0] == 2.0);
    BOOST_TEST(stepAdjointSensitivity[1] == 9.0);
    const auto forwardDot =
        stepDirectionalSensitivity[0] * stepAdjointSeed[0] +
        stepDirectionalSensitivity[1] * stepAdjointSeed[1];
    const auto adjointDot =
        stepDirectionalSeed[0] * stepAdjointSensitivity[0] +
        stepDirectionalSeed[1] * stepAdjointSensitivity[1];
    BOOST_TEST(forwardDot == adjointDot);

    derivativeInstance->end_simulation();
    derivativeInstance->get_directional_derivative(
        unknowns,
        stepKnowns,
        stepDirectionalSeed,
        stepDirectionalSensitivity);
    BOOST_TEST(stepDirectionalSensitivity[0] == -1.0);
    BOOST_TEST(stepDirectionalSensitivity[1] == 1.0);

    const auto outputDerivativePath =
        reference_fmu_directory() / "OutputDerivativesBouncingBall.fmu";
    require_fixture(outputDerivativePath);
    auto bouncingBall = std::static_pointer_cast<fmi::v3::fmu>(
        importer->import(outputDerivativePath));
    auto outputInstance =
        bouncingBall->instantiate_v3_slave("fmi3-output-derivatives");
    BOOST_TEST(outputInstance->max_output_derivative_order() == 1U);
    outputInstance->setup(
        to_time_point(0.0), to_time_point(1.0), std::nullopt);
    outputInstance->start_simulation();

    const value_reference outputs[] = {1, 3};
    const std::int32_t orders[] = {1, 1};
    double outputDerivatives[] = {0.0, 0.0};
    outputInstance->get_output_derivatives(
        outputs, orders, outputDerivatives);
    BOOST_TEST(outputDerivatives[0] == 0.0);
    BOOST_TEST(
        outputDerivatives[1] == -9.81,
        boost::test_tools::tolerance(1e-12));
    BOOST_CHECK_THROW(
        outputInstance->get_output_derivatives(
            outputs, orders, gsl::make_span(outputDerivatives, 1)),
        std::invalid_argument);

    const std::int32_t invalidOrder[] = {0, 1};
    BOOST_CHECK_THROW(
        outputInstance->get_output_derivatives(
            outputs, invalidOrder, outputDerivatives),
        std::invalid_argument);
    const value_reference localVariable = 2;
    const std::int32_t firstOrder = 1;
    BOOST_CHECK_EXCEPTION(
        outputInstance->get_output_derivatives(
            gsl::make_span(&localVariable, 1),
            gsl::make_span(&firstOrder, 1),
            gsl::make_span(outputDerivatives, 1)),
        cosim::error,
        error_is(errc::model_error));
    outputInstance->end_simulation();

    const auto unsupportedPath =
        reference_fmu_directory() / "Feedthrough.fmu";
    require_fixture(unsupportedPath);
    auto unsupported = std::static_pointer_cast<fmi::v3::fmu>(
        importer->import(unsupportedPath));
    auto unsupportedInstance =
        unsupported->instantiate_v3_slave("fmi3-unsupported-derivatives");
    BOOST_TEST(!unsupportedInstance->provides_directional_derivatives());
    BOOST_TEST(!unsupportedInstance->provides_adjoint_derivatives());
    BOOST_TEST(unsupportedInstance->max_output_derivative_order() == 0U);
    unsupportedInstance->setup(
        to_time_point(0.0), to_time_point(1.0), std::nullopt);
    BOOST_CHECK_EXCEPTION(
        unsupportedInstance->get_directional_derivative(
            gsl::make_span(unknowns),
            gsl::make_span(knowns),
            gsl::make_span(directionalSeed),
            gsl::make_span(directionalSensitivity)),
        cosim::error,
        error_is(errc::unsupported_feature));
}


BOOST_AUTO_TEST_CASE(fmi3_runtime_variable_dependencies)
{
    const auto fmuPath = fault_fixture("Fmi3Derivatives.fmu");

    auto importer = fmi::importer::create();
    auto fmu = std::static_pointer_cast<fmi::v3::fmu>(importer->import(fmuPath));
    auto instance = fmu->instantiate_v3_slave("fmi3-runtime-dependencies");
    BOOST_TEST(instance->provides_per_element_dependencies());

    // Initial dependencies are available before Initialization Mode exit.
    const auto matrix = instance->get_variable_dependencies(10);
    BOOST_REQUIRE_EQUAL(matrix.size(), 3U);
    BOOST_TEST(matrix[0].dependent_element == 1U);
    BOOST_CHECK((matrix[0].independent ==
        typed_value_reference{variable_type::real, 3}));
    BOOST_TEST(matrix[0].independent_element == 1U);
    BOOST_CHECK(matrix[0].kind == dependency_kind::dependent);
    BOOST_TEST(matrix[1].dependent_element == 2U);
    BOOST_TEST(matrix[1].independent_element == 2U);
    BOOST_CHECK(matrix[1].kind == dependency_kind::discrete);
    BOOST_TEST(matrix[2].dependent_element == 0U);
    BOOST_CHECK((matrix[2].independent ==
        typed_value_reference{variable_type::real, 1}));
    BOOST_TEST(matrix[2].independent_element == 0U);
    BOOST_CHECK(matrix[2].kind == dependency_kind::constant);

    BOOST_TEST(instance->get_variable_dependencies(11).empty());

    instance->setup(to_time_point(0.0), to_time_point(1.0), std::nullopt);
    instance->start_simulation();
    BOOST_CHECK_EQUAL(instance->get_variable_dependencies(10).size(), 3U);

    // The result follows structural parameters.
    BOOST_CHECK_EQUAL(instance->get_variable_dependencies(13).size(), 2U);
    instance->enter_configuration_mode();
    const value_reference sizeReference = 4;
    const std::uint64_t three = 3;
    instance->set_uint64_variables(
        gsl::make_span(&sizeReference, 1), gsl::make_span(&three, 1));
    instance->exit_configuration_mode();
    const auto structural = instance->get_variable_dependencies(13);
    BOOST_REQUIRE_EQUAL(structural.size(), 3U);
    BOOST_TEST(structural[2].dependent_element == 3U);
    BOOST_CHECK((structural[2].independent ==
        typed_value_reference{variable_type::real, 5}));

    BOOST_CHECK_EXCEPTION(
        instance->get_variable_dependencies(999),
        cosim::error,
        error_is(errc::model_error));
    BOOST_CHECK_EXCEPTION(
        instance->get_variable_dependencies(12),
        cosim::error,
        error_is(errc::model_error));
}


BOOST_AUTO_TEST_CASE(fmi3_runtime_variable_dependencies_require_capability)
{
    const auto fmuPath = reference_fmu_directory() / "Stair.fmu";
    require_fixture(fmuPath);
    auto importer = fmi::importer::create();
    auto fmu = std::static_pointer_cast<fmi::v3::fmu>(importer->import(fmuPath));
    auto instance = fmu->instantiate_v3_slave("fmi3-no-runtime-dependencies");
    BOOST_TEST(!instance->provides_per_element_dependencies());
    BOOST_CHECK_EXCEPTION(
        instance->get_variable_dependencies(1),
        cosim::error,
        error_is(errc::unsupported_feature));
}


BOOST_AUTO_TEST_CASE(fmi3_derivative_array_and_status_contract)
{
    const auto fmuPath = fault_fixture("Fmi3Derivatives.fmu");

    auto importer = fmi::importer::create();
    auto derivativeFmu = std::static_pointer_cast<fmi::v3::fmu>(
        importer->import(fmuPath));
    const auto makeInstance =
        [&derivativeFmu](const std::string& name) {
            auto instance = derivativeFmu->instantiate_v3_slave(name);
            instance->setup(
                to_time_point(0.0), to_time_point(1.0), std::nullopt);
            return instance;
        };

    auto instance = makeInstance("fmi3-derivative-contract");
    BOOST_TEST(instance->provides_directional_derivatives());
    BOOST_TEST(instance->provides_adjoint_derivatives());

    const value_reference matrixOutput = 10;
    const value_reference fixedInput = 3;
    const double matrixSeed[] = {1.0, 2.0, -1.0};
    double matrixSensitivity[6] = {};
    instance->get_directional_derivative(
        gsl::make_span(&matrixOutput, 1),
        gsl::make_span(&fixedInput, 1),
        matrixSeed,
        matrixSensitivity);
    const double expectedMatrixSensitivity[] =
        {2.0, 8.0, -2.0, 1.0, 9.0, -10.0};
    BOOST_CHECK_EQUAL_COLLECTIONS(
        std::begin(matrixSensitivity),
        std::end(matrixSensitivity),
        std::begin(expectedMatrixSensitivity),
        std::end(expectedMatrixSensitivity));

    const double matrixAdjointSeed[] =
        {1.0, 1.0, 1.0, 1.0, 1.0, 1.0};
    double matrixAdjointSensitivity[3] = {};
    instance->get_adjoint_derivative(
        gsl::make_span(&matrixOutput, 1),
        gsl::make_span(&fixedInput, 1),
        matrixAdjointSeed,
        matrixAdjointSensitivity);
    const double expectedMatrixAdjoint[] = {14.5, 2.5, 11.5};
    BOOST_CHECK_EQUAL_COLLECTIONS(
        std::begin(matrixAdjointSensitivity),
        std::end(matrixAdjointSensitivity),
        std::begin(expectedMatrixAdjoint),
        std::end(expectedMatrixAdjoint));

    const value_reference float32Output = 12;
    const value_reference mixedKnowns[] = {1, 2};
    const double mixedSeed[] = {2.0, 4.0};
    double mixedSensitivity = 0.0;
    instance->get_directional_derivative(
        gsl::make_span(&float32Output, 1),
        mixedKnowns,
        mixedSeed,
        gsl::make_span(&mixedSensitivity, 1));
    BOOST_TEST(mixedSensitivity == -6.0);

    const value_reference structuralOutput = 13;
    const value_reference structuralKnowns[] = {1, 5};
    const double structuralSeed[] = {3.0, 4.0, 5.0};
    double structuralSensitivity[2] = {};
    instance->get_directional_derivative(
        gsl::make_span(&structuralOutput, 1),
        structuralKnowns,
        structuralSeed,
        structuralSensitivity);
    BOOST_TEST(structuralSensitivity[0] == 7.0);
    BOOST_TEST(structuralSensitivity[1] == 13.0);

    const double structuralAdjointSeed[] = {2.0, 3.0};
    double structuralAdjointSensitivity[3] = {};
    instance->get_adjoint_derivative(
        gsl::make_span(&structuralOutput, 1),
        structuralKnowns,
        structuralAdjointSeed,
        structuralAdjointSensitivity);
    const double expectedStructuralAdjoint[] = {5.0, 2.0, 6.0};
    BOOST_CHECK_EQUAL_COLLECTIONS(
        std::begin(structuralAdjointSensitivity),
        std::end(structuralAdjointSensitivity),
        std::begin(expectedStructuralAdjoint),
        std::end(expectedStructuralAdjoint));

    const value_reference constantOutput = 11;
    const value_reference scalarKnown = 1;
    const double scalarSeed = 9.0;
    double zeroSensitivity = -1.0;
    instance->get_directional_derivative(
        gsl::make_span(&constantOutput, 1),
        gsl::make_span(&scalarKnown, 1),
        gsl::make_span(&scalarSeed, 1),
        gsl::make_span(&zeroSensitivity, 1));
    BOOST_TEST(zeroSensitivity == 0.0);

    const value_reference callCountReference = 101;
    std::uint64_t callCount = 0;
    instance->get_uint64_variables(
        gsl::make_span(&callCountReference, 1),
        gsl::make_span(&callCount, 1));
    BOOST_TEST(callCount == 6U);

    BOOST_CHECK_THROW(
        instance->get_directional_derivative(
            gsl::make_span(&matrixOutput, 1),
            gsl::make_span(&fixedInput, 1),
            gsl::make_span(matrixSeed, 2),
            matrixSensitivity),
        std::invalid_argument);
    BOOST_CHECK_THROW(
        instance->get_adjoint_derivative(
            gsl::make_span(&matrixOutput, 1),
            gsl::make_span(&fixedInput, 1),
            matrixAdjointSeed,
            gsl::make_span(matrixAdjointSensitivity, 2)),
        std::invalid_argument);

    const value_reference nonFloat = 102;
    BOOST_CHECK_EXCEPTION(
        instance->get_directional_derivative(
            gsl::make_span(&nonFloat, 1),
            gsl::make_span(&scalarKnown, 1),
            gsl::make_span(&scalarSeed, 1),
            gsl::make_span(&zeroSensitivity, 1)),
        cosim::error,
        error_is(errc::model_error));
    BOOST_CHECK_EXCEPTION(
        instance->get_directional_derivative(
            gsl::make_span(&matrixOutput, 1),
            gsl::make_span(&constantOutput, 1),
            gsl::make_span(&scalarSeed, 1),
            matrixSensitivity),
        cosim::error,
        error_is(errc::model_error));
    instance->get_uint64_variables(
        gsl::make_span(&callCountReference, 1),
        gsl::make_span(&callCount, 1));
    BOOST_TEST(callCount == 6U);

    instance->start_simulation();
    instance->enter_event_mode();
    instance->get_directional_derivative(
        gsl::make_span(&matrixOutput, 1),
        gsl::make_span(&fixedInput, 1),
        matrixSeed,
        matrixSensitivity);
    const auto update = instance->update_discrete_states();
    BOOST_TEST(!update.discrete_states_need_update);
    instance->enter_step_mode();
    instance->get_uint64_variables(
        gsl::make_span(&callCountReference, 1),
        gsl::make_span(&callCount, 1));
    const auto callsBeforeInvalidUnknown = callCount;
    BOOST_CHECK_EXCEPTION(
        instance->get_directional_derivative(
            gsl::make_span(&callCountReference, 1),
            gsl::make_span(&scalarKnown, 1),
            gsl::make_span(&scalarSeed, 1),
            gsl::make_span(&zeroSensitivity, 1)),
        cosim::error,
        error_is(errc::model_error));
    BOOST_CHECK_EXCEPTION(
        instance->get_adjoint_derivative(
            gsl::make_span(&callCountReference, 1),
            gsl::make_span(&scalarKnown, 1),
            gsl::make_span(&scalarSeed, 1),
            gsl::make_span(&zeroSensitivity, 1)),
        cosim::error,
        error_is(errc::model_error));
    instance->get_uint64_variables(
        gsl::make_span(&callCountReference, 1),
        gsl::make_span(&callCount, 1));
    BOOST_TEST(callCount == callsBeforeInvalidUnknown);
    instance->end_simulation();
    instance->get_adjoint_derivative(
        gsl::make_span(&matrixOutput, 1),
        gsl::make_span(&fixedInput, 1),
        matrixAdjointSeed,
        matrixAdjointSensitivity);

    auto zeroSizeInstance =
        derivativeFmu->instantiate_v3_slave("fmi3-zero-size-derivative");
    zeroSizeInstance->enter_configuration_mode();
    const value_reference structuralSizeReference = 4;
    const std::uint64_t zeroSize = 0;
    zeroSizeInstance->set_uint64_variables(
        gsl::make_span(&structuralSizeReference, 1),
        gsl::make_span(&zeroSize, 1));
    zeroSizeInstance->exit_configuration_mode();
    zeroSizeInstance->setup(
        to_time_point(0.0), to_time_point(1.0), std::nullopt);
    const value_reference structuralInput = 5;
    const gsl::span<const double> noSeed;
    gsl::span<double> noSensitivity;
    zeroSizeInstance->get_directional_derivative(
        gsl::make_span(&structuralOutput, 1),
        gsl::make_span(&structuralInput, 1),
        noSeed,
        noSensitivity);

    const value_reference statusReference = 100;
    {
        auto warningInstance = makeInstance("fmi3-derivative-warning");
        const int warning = 1;
        warningInstance->set_integer_variables(
            gsl::make_span(&statusReference, 1),
            gsl::make_span(&warning, 1));
        double warningSensitivity[6] = {};
        warningInstance->get_directional_derivative(
            gsl::make_span(&matrixOutput, 1),
            gsl::make_span(&fixedInput, 1),
            matrixSeed,
            warningSensitivity);
        BOOST_CHECK_EQUAL_COLLECTIONS(
            std::begin(warningSensitivity),
            std::end(warningSensitivity),
            std::begin(expectedMatrixSensitivity),
            std::end(expectedMatrixSensitivity));
    }

    for (const int failureStatus : std::array<int, 2>{2, 3}) {
        auto failureInstance = makeInstance(
            "fmi3-derivative-failure-" + std::to_string(failureStatus));
        failureInstance->set_integer_variables(
            gsl::make_span(&statusReference, 1),
            gsl::make_span(&failureStatus, 1));
        double failedSensitivity[6] =
            {-100.0, -100.0, -100.0, -100.0, -100.0, -100.0};
        BOOST_CHECK_EXCEPTION(
            failureInstance->get_directional_derivative(
                gsl::make_span(&matrixOutput, 1),
                gsl::make_span(&fixedInput, 1),
                matrixSeed,
                failedSensitivity),
            cosim::error,
            error_is(errc::model_error));
        BOOST_TEST(
            std::all_of(
                std::begin(failedSensitivity),
                std::end(failedSensitivity),
                [](double value) { return value == -100.0; }));
    }

    auto fatalImporter = fmi::importer::create();
    auto fatalFmu = std::static_pointer_cast<fmi::v3::fmu>(
        fatalImporter->import(fmuPath));
    auto fatalInstance =
        fatalFmu->instantiate_v3_slave("fmi3-derivative-fatal");
    auto fatalSibling =
        fatalFmu->instantiate_v3_slave("fmi3-derivative-fatal-sibling");
    fatalInstance->setup(
        to_time_point(0.0), to_time_point(1.0), std::nullopt);
    fatalSibling->setup(
        to_time_point(0.0), to_time_point(1.0), std::nullopt);
    const int fatal = 4;
    fatalInstance->set_integer_variables(
        gsl::make_span(&statusReference, 1),
        gsl::make_span(&fatal, 1));
    double fatalSensitivity[6] =
        {-100.0, -100.0, -100.0, -100.0, -100.0, -100.0};
    BOOST_CHECK_THROW(
        fatalInstance->get_directional_derivative(
            gsl::make_span(&matrixOutput, 1),
            gsl::make_span(&fixedInput, 1),
            matrixSeed,
            fatalSensitivity),
        cosim::error);
    BOOST_TEST(
        std::all_of(
            std::begin(fatalSensitivity),
            std::end(fatalSensitivity),
            [](double value) { return value == -100.0; }));
    BOOST_CHECK_THROW(
        fatalSibling->get_directional_derivative(
            gsl::make_span(&matrixOutput, 1),
            gsl::make_span(&fixedInput, 1),
            matrixSeed,
            fatalSensitivity),
        cosim::error);
}
