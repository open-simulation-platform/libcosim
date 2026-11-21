#define BOOST_TEST_MODULE system_structure unittests
#include "cosim/xml_value.hpp"
#include "mock_slave.hpp"
#include "scalar_value_stream_tests.hpp"

#include <cosim/algorithm/fixed_step_algorithm.hpp>
#include <cosim/exception.hpp>
#include <cosim/execution.hpp>
#include <cosim/function/linear_transformation.hpp>
#include <cosim/observer/last_value_observer.hpp>
#include <cosim/system_structure.hpp>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>


class mock_model : public cosim::model
{
public:
    std::shared_ptr<const cosim::model_description> description()
        const noexcept override
    {
        if (!nextSlave_) nextSlave_ = std::make_shared<mock_slave>();
        return std::make_shared<cosim::model_description>(
            nextSlave_->model_description());
    }

    std::shared_ptr<cosim::slave> instantiate(std::string_view /*name*/)
        override
    {
        if (!nextSlave_) nextSlave_ = std::make_shared<mock_slave>();
        return std::move(nextSlave_);
    }

private:
    mutable std::shared_ptr<mock_slave> nextSlave_;
};

BOOST_AUTO_TEST_CASE(custom_model_instantiation)
{
    auto model = std::make_shared<mock_model>();
    cosim::system_structure structure;
    structure.add_entity("first", model);
    structure.add_entity("second", model);
    auto algorithm = std::make_shared<cosim::fixed_step_algorithm>(cosim::to_duration(0.1));
    cosim::execution execution(cosim::to_time_point(0), algorithm);
    std::vector<std::string> names;
    const auto indices = cosim::inject_system_structure(execution, structure, {},
        [&names, &model](const std::shared_ptr<cosim::model>& selected, std::string_view name) {
            BOOST_CHECK(selected == model);
            names.emplace_back(name);
            return selected->instantiate(name);
        });
    BOOST_CHECK_EQUAL(names.size(), 2U);
    BOOST_CHECK_EQUAL(indices.simulators.size(), 2U);
    BOOST_CHECK(execution.get_slave(indices.simulators.at("first")));
    BOOST_CHECK(execution.get_slave(indices.simulators.at("second")));

    auto failingAlgorithm = std::make_shared<cosim::fixed_step_algorithm>(cosim::to_duration(0.1));
    cosim::execution failingExecution(cosim::to_time_point(0), failingAlgorithm);
    BOOST_CHECK_THROW(cosim::inject_system_structure(failingExecution, structure, {},
        [](const std::shared_ptr<cosim::model>&, std::string_view) {
            return std::shared_ptr<cosim::slave>{};
        }), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(missing_model_description_is_reported)
{
    class unavailable_model : public cosim::model
    {
    public:
        bool available = false;
        std::shared_ptr<const cosim::model_description> description() const noexcept override
        {
            return available ? description_ : nullptr;
        }
        std::shared_ptr<cosim::slave> instantiate(std::string_view) override
        {
            return std::make_shared<mock_slave>();
        }
        std::shared_ptr<const cosim::model_description> description_ =
            std::make_shared<cosim::model_description>(mock_slave().model_description());
    };

    auto source = std::make_shared<unavailable_model>();
    auto target = std::make_shared<unavailable_model>();
    cosim::system_structure structure;
    BOOST_CHECK_THROW(structure.add_entity("source", source), cosim::error);
    source->available = target->available = true;
    structure.add_entity("source", source);
    structure.add_entity("target", target);
    source->available = false;
    BOOST_CHECK_THROW(
        structure.get_variable_description({"source", "realOut"}), cosim::error);
    BOOST_CHECK_THROW(
        structure.connect_variables({"source", "realOut"}, {"target", "realIn"}), cosim::error);
}

BOOST_AUTO_TEST_CASE(nested_alias_name_lookup)
{
    auto description = std::make_shared<cosim::model_description>();
    description->uuid = "alias-model";
    cosim::variable_description variable{};
    variable.name = "position";
    variable.reference = 42;
    variable.type = cosim::variable_type::real;
    variable.causality = cosim::variable_causality::output;
    variable.variability = cosim::variable_variability::continuous;
    variable.description = "Position";
    variable.display_unit = "m";
    variable.aliases = {
        {"position_ft", std::string("Position in feet"), std::string("ft")},
        {"position_copy", std::nullopt, std::nullopt}};
    description->variables.push_back(variable);

    const auto alias = cosim::find_variable(*description, "position_ft");
    BOOST_REQUIRE(alias);
    BOOST_TEST(alias->name == "position_ft");
    BOOST_TEST(alias->reference == 42U);
    BOOST_TEST(alias->description == "Position in feet");
    BOOST_REQUIRE(alias->display_unit);
    BOOST_TEST(*alias->display_unit == "ft");
    BOOST_TEST(alias->aliases.empty());
    const auto copy = cosim::find_variable(*description, "position_copy");
    BOOST_REQUIRE(copy);
    BOOST_TEST(copy->description.empty());
    BOOST_REQUIRE(copy->display_unit);
    BOOST_TEST(*copy->display_unit == "m");
    BOOST_TEST(!cosim::find_variable(*description, "missing"));

    class metadata_model : public cosim::model
    {
    public:
        explicit metadata_model(std::shared_ptr<const cosim::model_description> md)
            : description_(std::move(md)) {}
        std::shared_ptr<const cosim::model_description> description()
            const noexcept override { return description_; }
        std::shared_ptr<cosim::slave> instantiate(std::string_view) override
        {
            throw std::logic_error("Metadata-only model cannot be instantiated");
        }
    private:
        std::shared_ptr<const cosim::model_description> description_;
    };
    cosim::system_structure system;
    system.add_entity("sim", std::make_shared<metadata_model>(description));
    const auto& cached = system.get_variable_description({"sim", "position_ft"});
    BOOST_TEST(cached.reference == 42U);
    BOOST_TEST(cached.name == "position_ft");
    BOOST_TEST(cached.description == "Position in feet");
    BOOST_REQUIRE(cached.display_unit);
    BOOST_TEST(*cached.display_unit == "ft");
    description->variables.front().aliases.push_back({"position", std::nullopt, std::nullopt});
    cosim::system_structure invalid;
    BOOST_CHECK_THROW(
        invalid.add_entity("sim", std::make_shared<metadata_model>(description)),
        cosim::error);
}


BOOST_AUTO_TEST_CASE(scalar_value_views)
{
    using byte_view = gsl::span<const std::uint8_t>;
    static_assert(std::is_same_v<decltype(std::declval<byte_view>()[0]), const std::uint8_t&>);

    cosim::binary_value binary{{0x01, 0x7f, 0xff}};
    cosim::scalar_value_view view{byte_view{binary.data}};
    const auto bytes = std::get<byte_view>(view);
    BOOST_REQUIRE_EQUAL(bytes.size(), binary.data.size());
    BOOST_CHECK(bytes.data() == binary.data.data());
    BOOST_CHECK_EQUAL(static_cast<unsigned int>(bytes[0]), 0x01U);
    binary.data[1] = 0x42;
    BOOST_CHECK_EQUAL(static_cast<unsigned int>(bytes[1]), 0x42U);

    view = byte_view{};
    BOOST_CHECK(std::get<byte_view>(view).empty());
    const std::string text = "value";
    view = std::string_view{text};
    BOOST_CHECK(std::get<std::string_view>(view) == "value");
    view = 2.5;
    BOOST_CHECK_EQUAL(std::get<double>(view), 2.5);
}


BOOST_AUTO_TEST_CASE(scalar_value_type_validation)
{
    using cosim::scalar_value;
    using cosim::variable_type;

    const std::array<std::pair<scalar_value, variable_type>, 14> values{{
        {scalar_value(1.0), variable_type::real},
        {scalar_value(1), variable_type::integer},
        {scalar_value(true), variable_type::boolean},
        {scalar_value(std::string("value")), variable_type::string},
        {scalar_value(cosim::enumeration_value{1}), variable_type::enumeration},
        {scalar_value(1.0F), variable_type::float32},
        {scalar_value(std::int8_t{-1}), variable_type::int8},
        {scalar_value(std::uint8_t{1}), variable_type::uint8},
        {scalar_value(std::int16_t{-1}), variable_type::int16},
        {scalar_value(std::uint16_t{1}), variable_type::uint16},
        {scalar_value(std::uint32_t{1}), variable_type::uint32},
        {scalar_value(std::int64_t{-1}), variable_type::int64},
        {scalar_value(std::uint64_t{1}), variable_type::uint64},
        {scalar_value(cosim::binary_value{{0x01}}), variable_type::binary},
    }};
    BOOST_CHECK_EQUAL(values.size(), std::variant_size_v<scalar_value>);

    cosim::variable_description variable{};
    for (const auto& [value, expectedType] : values) {
        BOOST_TEST_CONTEXT("value type: " << cosim::to_text(expectedType)) {
            BOOST_CHECK(cosim::type_of(value) == expectedType);
            for (const auto& [unusedValue, targetType] : values) {
                (void)unusedValue;
                variable.type = targetType;
                const bool expectedValid = expectedType == targetType ||
                    (expectedType == variable_type::integer &&
                        targetType == variable_type::enumeration);
                std::string reason = "unchanged";
                BOOST_TEST_CONTEXT("target type: " << cosim::to_text(targetType)) {
                    BOOST_CHECK(
                        cosim::is_valid_variable_value(variable, value, &reason) ==
                        expectedValid);
                    BOOST_CHECK(
                        cosim::is_valid_variable_value(variable, value, nullptr) ==
                        expectedValid);
                    if (expectedValid) {
                        BOOST_CHECK_EQUAL(reason, "unchanged");
                    } else {
                        const std::string expectedReason =
                            std::string("Cannot assign a value of type '") +
                            cosim::to_text(expectedType) +
                            "' to a variable of type '" +
                            cosim::to_text(targetType) + "'.";
                        BOOST_CHECK_EQUAL(reason, expectedReason);
                    }
                }
            }
            variable.type = variable_type::clock;
            std::string reason;
            BOOST_CHECK(!cosim::is_valid_variable_value(variable, value, &reason));
            const std::string expectedReason =
                std::string("Cannot assign a value of type '") +
                cosim::to_text(expectedType) +
                "' to a variable of type 'clock'.";
            BOOST_CHECK_EQUAL(reason, expectedReason);
        }
    }

    variable.type = variable_type::enumeration;
    variable.dimensions = {cosim::fixed_dimension{2}};
    std::string reason;
    BOOST_CHECK(!cosim::is_valid_variable_value(variable, scalar_value(1), &reason));
    BOOST_CHECK_EQUAL(
        reason,
        "Cannot assign a value of type 'integer' to a variable of type 'enumeration'.");

    const cosim::variable_value enumerationArray(
        cosim::enumeration_values{{{1}, {2}}}, {2});
    BOOST_CHECK(cosim::is_valid_variable_value(variable, enumerationArray, nullptr));
    const cosim::variable_value integerArray(cosim::integer_values{{1, 2}}, {2});
    BOOST_CHECK(!cosim::is_valid_variable_value(variable, integerArray, &reason));
    BOOST_CHECK_EQUAL(
        reason,
        "Cannot assign a value of type 'integer' to a variable of type 'enumeration'.");
    const cosim::variable_value enumerationScalar(cosim::enumeration_values{{{1}}});
    BOOST_CHECK(!cosim::is_valid_variable_value(variable, enumerationScalar, &reason));
    BOOST_CHECK_EQUAL(reason, "Value dimensions differ from the variable dimensions.");
}


BOOST_AUTO_TEST_CASE(exact_variable_values)
{
    BOOST_CHECK(cosim::type_of(cosim::scalar_value(1.0F)) ==
        cosim::variable_type::float32);
    BOOST_CHECK(
        cosim::type_of(cosim::scalar_value(std::uint64_t{42})) ==
        cosim::variable_type::uint64);
    BOOST_CHECK(
        cosim::type_of(cosim::scalar_value(cosim::enumeration_value{7})) ==
        cosim::variable_type::enumeration);
    BOOST_CHECK(
        cosim::type_of(cosim::scalar_value(cosim::binary_value{{0x01, 0xaf}})) ==
        cosim::variable_type::binary);

    const cosim::variable_value array(
        cosim::float32_values{{1.0F, 2.0F, 3.0F, 4.0F, 5.0F, 6.0F}},
        {2, 3});
    BOOST_CHECK(array.type() == cosim::variable_type::float32);
    BOOST_CHECK(!array.is_scalar());
    BOOST_CHECK(array.size() == 6);
    BOOST_CHECK(array.dimensions() == std::vector<std::uint64_t>({2, 3}));

    const cosim::variable_value scalar(cosim::int8_values{{-1}});
    BOOST_CHECK(scalar.type() == cosim::variable_type::int8);
    BOOST_CHECK(scalar.is_scalar());
    BOOST_CHECK(scalar.size() == 1);

    BOOST_CHECK_THROW(
        cosim::variable_value(cosim::uint16_values{{1, 2}}, {3}),
        std::invalid_argument);
    BOOST_CHECK_THROW(
        cosim::flat_size(
            {std::numeric_limits<std::uint64_t>::max(),
                std::numeric_limits<std::uint64_t>::max()}),
        std::overflow_error);

    std::ostringstream binaryText;
    binaryText << cosim::binary_value{{0x00, 0x0f, 0xa5, 0xff}};
    BOOST_CHECK_EQUAL(binaryText.str(), "000fa5ff");

    cosim::variable_description fixedArray{};
    fixedArray.name = "array";
    fixedArray.reference = 1;
    fixedArray.type = cosim::variable_type::float32;
    fixedArray.causality = cosim::variable_causality::input;
    fixedArray.variability = cosim::variable_variability::continuous;
    fixedArray.dimensions = {
        cosim::fixed_dimension{2},
        cosim::fixed_dimension{3}};
    BOOST_CHECK(
        cosim::fixed_shape(fixedArray) ==
        std::optional<std::vector<std::uint64_t>>({{2, 3}}));
    BOOST_CHECK(cosim::is_valid_variable_value(fixedArray, array, nullptr));

    fixedArray.dimensions[1] = cosim::variable_dimension{2};
    BOOST_CHECK(!cosim::fixed_shape(fixedArray));
}


BOOST_AUTO_TEST_CASE(variable_value_shapes_and_defaults)
{
    const auto large = std::numeric_limits<std::uint64_t>::max();
    BOOST_CHECK_EQUAL(cosim::flat_size({}), 1U);
    BOOST_CHECK_EQUAL(cosim::flat_size({2, 3}), 6U);
    BOOST_CHECK_EQUAL(cosim::flat_size({0, 3}), 0U);
    BOOST_CHECK_EQUAL(cosim::flat_size({2, 0, 3}), 0U);
    BOOST_CHECK_EQUAL(cosim::flat_size({large, large, 0}), 0U);
    BOOST_CHECK_EQUAL(cosim::flat_size({0, large, large}), 0U);
    BOOST_CHECK_THROW(cosim::flat_size({large, 2}), std::overflow_error);
    if constexpr (sizeof(std::size_t) < sizeof(std::uint64_t)) {
        BOOST_CHECK_THROW(cosim::flat_size({large}), std::overflow_error);
    }

    const cosim::variable_value defaultValue;
    BOOST_CHECK(defaultValue.type() == cosim::variable_type::real);
    BOOST_CHECK(defaultValue.is_scalar());
    BOOST_CHECK_EQUAL(defaultValue.size(), 1U);
    BOOST_CHECK(defaultValue.dimensions().empty());
    const auto& defaultValues = std::get<cosim::real_values>(defaultValue.values()).values;
    BOOST_REQUIRE_EQUAL(defaultValues.size(), 1U);
    BOOST_CHECK_EQUAL(defaultValues.front(), 0.0);

    const cosim::variable_value emptyArray(
        cosim::real_values{std::vector<double>{}}, {large, large, 0});
    BOOST_CHECK(!emptyArray.is_scalar());
    BOOST_CHECK_EQUAL(emptyArray.size(), 0U);
    BOOST_CHECK_THROW(
        cosim::variable_value(cosim::real_values{{1.0}}, {0}),
        std::invalid_argument);
    BOOST_CHECK_THROW(
        cosim::variable_value(cosim::real_values{{1.0}}, {large, 2}),
        std::overflow_error);

    cosim::variable_description variable{};
    const auto scalarShape = cosim::fixed_shape(variable);
    BOOST_REQUIRE(scalarShape);
    BOOST_CHECK(scalarShape->empty());
    variable.dimensions = {cosim::fixed_dimension{2}, cosim::fixed_dimension{3}};
    const auto fixedShape = cosim::fixed_shape(variable);
    BOOST_REQUIRE(fixedShape);
    BOOST_CHECK(*fixedShape == std::vector<std::uint64_t>({2, 3}));
    variable.dimensions = {
        cosim::fixed_dimension{large}, cosim::fixed_dimension{large},
        cosim::fixed_dimension{0}};
    const auto emptyShape = cosim::fixed_shape(variable);
    BOOST_REQUIRE(emptyShape);
    BOOST_CHECK(*emptyShape == std::vector<std::uint64_t>({large, large, 0}));
    variable.dimensions.pop_back();
    BOOST_CHECK_THROW(cosim::fixed_shape(variable), std::overflow_error);
    variable.dimensions.push_back(cosim::variable_dimension{42});
    BOOST_CHECK(!cosim::fixed_shape(variable));

    const cosim::clock_description clock;
    BOOST_CHECK(clock.interval_variability == cosim::clock_interval_variability::unknown);
}


BOOST_AUTO_TEST_CASE(clock_connection_requires_triggered_metadata)
{
    cosim::variable_description source{};
    source.type = cosim::variable_type::clock;
    source.causality = cosim::variable_causality::output;
    source.clock.emplace();
    auto target = source;
    target.causality = cosim::variable_causality::input;

    std::string reason;
    BOOST_CHECK(!cosim::is_valid_connection(source, target, &reason));
    BOOST_CHECK_EQUAL(reason, "Clock connections require triggered source and target Clocks.");
    source.clock->interval_variability = cosim::clock_interval_variability::triggered;
    BOOST_CHECK(!cosim::is_valid_connection(source, target, nullptr));
    target.clock->interval_variability = cosim::clock_interval_variability::triggered;
    BOOST_CHECK(cosim::is_valid_connection(source, target, nullptr));
    source.clock->interval_variability = cosim::clock_interval_variability::unknown;
    BOOST_CHECK(!cosim::is_valid_connection(source, target, nullptr));
}


BOOST_AUTO_TEST_CASE(exact_connection_shapes)
{
    cosim::variable_description simulatorOutput{};
    simulatorOutput.name = "output";
    simulatorOutput.reference = 1;
    simulatorOutput.type = cosim::variable_type::uint64;
    simulatorOutput.causality = cosim::variable_causality::output;
    simulatorOutput.variability = cosim::variable_variability::discrete;
    simulatorOutput.dimensions = {cosim::fixed_dimension{2}};
    auto simulatorInput = simulatorOutput;
    simulatorInput.name = "input";
    simulatorInput.reference = 2;
    simulatorInput.causality = cosim::variable_causality::input;

    cosim::function_io_description functionInput{
        "",
        cosim::variable_type::uint64,
        cosim::variable_causality::input,
        1,
        {std::uint64_t{2}}};
    cosim::function_io_description functionOutput{
        "",
        cosim::variable_type::uint64,
        cosim::variable_causality::output,
        1,
        {std::uint64_t{2}}};

    BOOST_CHECK(cosim::is_valid_connection(
        simulatorOutput, simulatorInput, nullptr));
    BOOST_CHECK(cosim::is_valid_connection(
        simulatorOutput, functionInput, nullptr));
    BOOST_CHECK(cosim::is_valid_connection(
        functionOutput, simulatorInput, nullptr));
    BOOST_CHECK(cosim::is_valid_connection(
        functionOutput, functionInput, nullptr));

    simulatorInput.dimensions = {cosim::fixed_dimension{3}};
    BOOST_CHECK(!cosim::is_valid_connection(
        simulatorOutput, simulatorInput, nullptr));
    BOOST_CHECK(!cosim::is_valid_connection(
        functionOutput, simulatorInput, nullptr));
    functionInput.dimensions = {std::uint64_t{3}};
    BOOST_CHECK(!cosim::is_valid_connection(
        simulatorOutput, functionInput, nullptr));
    BOOST_CHECK(!cosim::is_valid_connection(
        functionOutput, functionInput, nullptr));

    simulatorOutput.dimensions = {cosim::variable_dimension{99}};
    BOOST_CHECK(cosim::is_valid_connection(
        simulatorOutput, functionInput, nullptr));
    functionInput.dimensions.clear();
    BOOST_CHECK(!cosim::is_valid_connection(
        simulatorOutput, functionInput, nullptr));
    functionInput.dimensions = {std::uint64_t{3}, std::uint64_t{1}};
    BOOST_CHECK(!cosim::is_valid_connection(
        simulatorOutput, functionInput, nullptr));

    functionOutput.dimensions = {
        cosim::function_parameter_placeholder{0}};
    BOOST_CHECK(!cosim::is_valid_connection(
        functionOutput, functionInput, nullptr));
}


BOOST_AUTO_TEST_CASE(enumeration_connection_domains)
{
    const auto makeModel = [](std::string typeName,
                               std::vector<std::int64_t> items,
                               cosim::variable_causality causality) {
        cosim::model_description model;
        cosim::variable_type_definition definition;
        definition.name = std::move(typeName);
        definition.type = cosim::variable_type::enumeration;
        for (const auto value : items) {
            definition.enumeration_items.push_back(
                {std::to_string(value), value, {}});
        }
        model.type_definitions.push_back(std::move(definition));

        cosim::variable_description variable{};
        variable.name = causality == cosim::variable_causality::output
            ? "output"
            : "input";
        variable.reference = 1;
        variable.type = cosim::variable_type::enumeration;
        variable.causality = causality;
        variable.variability = cosim::variable_variability::discrete;
        variable.declared_type = model.type_definitions.front().name;
        model.variables.push_back(std::move(variable));
        return model;
    };

    const auto source = makeModel(
        "SourceOption", {1, 2}, cosim::variable_causality::output);
    auto target = makeModel(
        "TargetOption", {1, 2, 3}, cosim::variable_causality::input);
    std::string reason;
    BOOST_CHECK(cosim::is_valid_connection(
        source,
        source.variables.front(),
        target,
        target.variables.front(),
        &reason));

    target.type_definitions.front().enumeration_items.erase(
        target.type_definitions.front().enumeration_items.begin() + 1);
    BOOST_CHECK(!cosim::is_valid_connection(
        source,
        source.variables.front(),
        target,
        target.variables.front(),
        &reason));
    BOOST_TEST(reason.find("value 2") != std::string::npos);

    auto boundedSource = source;
    boundedSource.variables.front().max = cosim::enumeration_value{1};
    BOOST_CHECK(cosim::is_valid_connection(
        boundedSource,
        boundedSource.variables.front(),
        target,
        target.variables.front(),
        &reason));
}


BOOST_AUTO_TEST_CASE(system_structure_basic_use)
{
    // Some test parameters
    constexpr auto startTime = cosim::time_point();
    constexpr auto stopTime = cosim::time_point(std::chrono::seconds(1));
    constexpr auto timeStep = std::chrono::milliseconds(100);
    constexpr auto offset = 2.0;
    constexpr auto factor = 3.0;

    // Get a model and function type
    const auto model = std::make_shared<mock_model>();
    const auto func = std::make_shared<cosim::linear_transformation_function_type>();
    const auto funcParams = cosim::function_parameter_value_map{
        std::make_pair(cosim::linear_transformation_function_type::offset_parameter_index, offset),
        std::make_pair(cosim::linear_transformation_function_type::factor_parameter_index, factor)};

    // Set up a system structure, check basic functionality
    cosim::system_structure ss;

    ss.add_entity("simA", model);
    ss.add_entity("simB", model, timeStep * 2);
    ss.add_entity("func", func, funcParams);
    ss.add_entity("simC", model);
    BOOST_CHECK_THROW(ss.add_entity("simB", model), cosim::error); // simB exists
    BOOST_CHECK_THROW(ss.add_entity("func", func, funcParams), cosim::error); // func exists
    BOOST_CHECK_THROW(ss.add_entity("", model), cosim::error); // invalid name
    BOOST_CHECK_THROW(ss.add_entity("simB", model, -timeStep), cosim::error); // negative step size

    ss.connect_variables({"simA", "realOut"}, {"simB", "realIn"}); // sim-sim connection
    ss.connect_variables({"simA", "realOut"}, {"simA", "realIn"}); // sim self connection
    ss.connect_variables({"simB", "realOut"}, {"func", "in", 0, "", 0}); // sim-func connection
    ss.connect_variables({"func", "out", 0, "", 0}, {"simC", "realIn"}); // func-sim connection
    BOOST_CHECK_THROW(
        ss.connect_variables({"simB", "realOut"}, {"simB", "realIn"}),
        cosim::error); // simB.realIn already connected
    BOOST_CHECK_THROW(
        ss.connect_variables({"simC", "realOut"}, {"func", "in", 0, "", 0}),
        cosim::error); // func.in already connected
    BOOST_CHECK_THROW(
        ss.connect_variables({"simA", "realOut"}, {"simB", "intIn"}),
        cosim::error); // incompatible variables
    BOOST_CHECK_THROW(
        ss.get_function_io_description({"func", "in", 1, "", 0}),
        cosim::error);
    BOOST_CHECK_THROW(
        ss.get_function_io_description({"func", "in", 0, "", 1}),
        cosim::error);

    const auto entities = ss.entities();
    BOOST_TEST_REQUIRE(std::distance(entities.begin(), entities.end()) == 4);
    const auto conns = ss.connections();
    BOOST_TEST_REQUIRE(std::distance(conns.begin(), conns.end()) == 4);

    // Initial values
    constexpr auto initialInput = 11.0;
    cosim::variable_value_map initialValues;
    cosim::add_variable_value(initialValues, ss, {"simA", "realIn"}, initialInput);
    BOOST_CHECK_THROW(
        cosim::add_variable_value(initialValues, ss, {"noneSuch", "realIn"}, 1.0),
        cosim::error);
    BOOST_CHECK_THROW(
        cosim::add_variable_value(initialValues, ss, {"simA", "noneSuch"}, 1.0),
        cosim::error);
    BOOST_CHECK_THROW(
        cosim::add_variable_value(initialValues, ss, {"simA", "realIn"}, "a string"),
        cosim::error);
    BOOST_CHECK(initialValues.size() == 1);

    // Set up and run an execution to verify that the system structure
    // turns out as intended.
    auto execution = cosim::execution(
        startTime, std::make_shared<cosim::fixed_step_algorithm>(timeStep));
    const auto obs = std::make_shared<cosim::last_value_observer>();
    execution.add_observer(obs);

    const auto entityIndexes =
        cosim::inject_system_structure(execution, ss, initialValues);
    BOOST_CHECK(entityIndexes.simulators.size() == 3);
    BOOST_CHECK(entityIndexes.functions.size() == 1);

    BOOST_CHECK(execution.simulate_until(stopTime));

    const auto realOutRef = mock_slave::real_out_reference;
    double realOutA = -1.0, realOutB = -1.0, realOutC = -1.0;
    obs->get_real(
        entityIndexes.simulators.at("simA"),
        gsl::make_span(&realOutRef, 1),
        gsl::make_span(&realOutA, 1));
    obs->get_real(
        entityIndexes.simulators.at("simB"),
        gsl::make_span(&realOutRef, 1),
        gsl::make_span(&realOutB, 1));
    obs->get_real(
        entityIndexes.simulators.at("simC"),
        gsl::make_span(&realOutRef, 1),
        gsl::make_span(&realOutC, 1));
    BOOST_CHECK(realOutA == initialInput);
    BOOST_CHECK(realOutB == initialInput);
    BOOST_CHECK(realOutC == offset + factor * initialInput);
}

BOOST_AUTO_TEST_SUITE(xml_values)

using cosim::detail::parse_xml_scalar_value;
using cosim::variable_type;

BOOST_AUTO_TEST_CASE(exact_integer_values)
{
    BOOST_TEST(std::get<int>(parse_xml_scalar_value(variable_type::integer, "+2147483647")) == 2147483647);
    BOOST_TEST(+std::get<std::int8_t>(parse_xml_scalar_value(variable_type::int8, "-128")) == -128);
    BOOST_TEST(+std::get<std::uint8_t>(parse_xml_scalar_value(variable_type::uint8, " \t255\n")) == 255);
    BOOST_TEST(std::get<std::int16_t>(parse_xml_scalar_value(variable_type::int16, "-32768")) == -32768);
    BOOST_TEST(std::get<std::uint16_t>(parse_xml_scalar_value(variable_type::uint16, "65535")) == 65535);
    BOOST_TEST(std::get<std::uint32_t>(parse_xml_scalar_value(variable_type::uint32, "4294967295")) == 4294967295U);
    BOOST_TEST(std::get<std::int64_t>(parse_xml_scalar_value(variable_type::int64, "-9223372036854775808")) ==
        (std::numeric_limits<std::int64_t>::min)());
    BOOST_TEST(std::get<std::uint64_t>(parse_xml_scalar_value(variable_type::uint64, "18446744073709551615")) ==
        (std::numeric_limits<std::uint64_t>::max)());
    BOOST_TEST(+std::get<std::uint8_t>(parse_xml_scalar_value(variable_type::uint8, "-00")) == 0);
    BOOST_TEST(std::get<cosim::enumeration_value>(parse_xml_scalar_value(variable_type::enumeration, "-2")).value == -2);
}

BOOST_AUTO_TEST_CASE(rejects_invalid_and_out_of_range_numbers)
{
    BOOST_CHECK_THROW(parse_xml_scalar_value(variable_type::int8, "128"), std::out_of_range);
    BOOST_CHECK_THROW(parse_xml_scalar_value(variable_type::uint8, "256"), std::out_of_range);
    BOOST_CHECK_THROW(parse_xml_scalar_value(variable_type::int64, "-9223372036854775809"), std::out_of_range);
    BOOST_CHECK_THROW(parse_xml_scalar_value(variable_type::uint64, "18446744073709551616"), std::out_of_range);
    BOOST_CHECK_THROW(parse_xml_scalar_value(variable_type::uint64, "-1"), std::invalid_argument);
    BOOST_CHECK_THROW(parse_xml_scalar_value(variable_type::integer, "+-1"), std::invalid_argument);
    BOOST_CHECK_THROW(parse_xml_scalar_value(variable_type::integer, "0x0F"), std::invalid_argument);
    BOOST_CHECK_THROW(parse_xml_scalar_value(variable_type::integer, " "), std::invalid_argument);
    BOOST_CHECK_THROW(parse_xml_scalar_value(variable_type::float32, "1e100"), std::out_of_range);
    BOOST_CHECK_THROW(parse_xml_scalar_value(variable_type::real, "1e"), std::invalid_argument);
    BOOST_CHECK_THROW(parse_xml_scalar_value(variable_type::real, "+INF"), std::invalid_argument);
    BOOST_CHECK_THROW(parse_xml_scalar_value(variable_type::real, "nan(x)"), std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(floating_point_boolean_and_string_values)
{
    BOOST_TEST(std::get<float>(parse_xml_scalar_value(variable_type::float32, "+.125E1")) == 1.25f);
    BOOST_TEST(std::get<double>(parse_xml_scalar_value(variable_type::real, "16777217")) == 16777217.0);
    BOOST_TEST(std::signbit(std::get<float>(parse_xml_scalar_value(variable_type::float32, "-0"))));
    BOOST_TEST(std::get<double>(parse_xml_scalar_value(variable_type::real, "INF")) ==
        std::numeric_limits<double>::infinity());
    BOOST_TEST(std::get<float>(parse_xml_scalar_value(variable_type::float32, "-INF")) ==
        -std::numeric_limits<float>::infinity());
    BOOST_TEST(std::isnan(std::get<double>(parse_xml_scalar_value(variable_type::real, "NaN"))));
    BOOST_TEST(std::get<bool>(parse_xml_scalar_value(variable_type::boolean, " true ")));
    BOOST_TEST(std::get<bool>(parse_xml_scalar_value(variable_type::boolean, "1")));
    BOOST_TEST(!std::get<bool>(parse_xml_scalar_value(variable_type::boolean, "false")));
    BOOST_TEST(!std::get<bool>(parse_xml_scalar_value(variable_type::boolean, "0")));
    BOOST_CHECK_THROW(parse_xml_scalar_value(variable_type::boolean, "True"), std::invalid_argument);
    BOOST_TEST(std::get<std::string>(parse_xml_scalar_value(variable_type::string, " a b  ")) == " a b  ");
}

BOOST_AUTO_TEST_CASE(binary_and_clock_values)
{
    const auto value = std::get<cosim::binary_value>(parse_xml_scalar_value(variable_type::binary, "\t00A5fF "));
    const std::vector<std::uint8_t> expected{0, 0xA5, 0xFF};
    BOOST_CHECK(value.data == expected);
    BOOST_TEST(std::get<cosim::binary_value>(parse_xml_scalar_value(variable_type::binary, "")).data.empty());
    BOOST_CHECK_THROW(parse_xml_scalar_value(variable_type::binary, "F"), std::invalid_argument);
    BOOST_CHECK_THROW(parse_xml_scalar_value(variable_type::binary, "0G"), std::invalid_argument);
    BOOST_CHECK_THROW(parse_xml_scalar_value(variable_type::binary, "00 FF "), std::invalid_argument);
    BOOST_CHECK_THROW(parse_xml_scalar_value(variable_type::clock, "1"), std::invalid_argument);
}

BOOST_AUTO_TEST_SUITE_END()
