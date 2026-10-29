#include "cosim/fmi/enumeration.hpp"
#include "cosim/fmi/glue.hpp"

// Reject malformed or ambiguous secondary XML with contextual errors instead
// of silently dropping metadata; valid unit and Enumeration restrictions survive.
BOOST_AUTO_TEST_CASE(v2_secondary_metadata_errors)
{
    utility::temp_dir directory;
    const auto path = directory.path() / "modelDescription.xml";
    const auto write = [&path](const std::string& xml) {
        std::ofstream output(path);
        BOOST_REQUIRE(output);
        output << xml;
    };
    const auto bad = [&](const std::string& xml, const std::string& message) {
        write(xml);
        std::vector<unit_definition> units;
        fmi::fmi2_enumeration_restrictions restrictions;
        BOOST_CHECK_EXCEPTION(
            fmi::read_xml_metadata(units, path, &restrictions),
            cosim::error,
            [&](const cosim::error& error) {
                return error.code() == make_error_code(errc::bad_file) &&
                    std::string(error.what()).find(message) != std::string::npos;
            });
    };
    bad("<fmiModelDescription><broken>", path.string());
    bad("<fmiModelDescription><UnitDefinitions><Unit/></UnitDefinitions></fmiModelDescription>",
        "Unnamed FMI unit definition");
    bad("<fmiModelDescription><UnitDefinitions><Unit name=\"\"/></UnitDefinitions></fmiModelDescription>",
        "Unnamed FMI unit definition");
    bad("<fmiModelDescription><UnitDefinitions><Unit name=\"u\"/><Unit name=\"u\"/></UnitDefinitions></fmiModelDescription>",
        "Duplicate FMI unit definition 'u'");
    bad("<fmiModelDescription><ModelVariables><ScalarVariable name=\"x\"><Enumeration min=\"bad\"/></ScalarVariable></ModelVariables></fmiModelDescription>",
        "Invalid Enumeration bound");

    write("<fmiModelDescription><UnitDefinitions><Unit name=\"u\"><BaseUnit/></Unit><Unit name=\"opaque\"/></UnitDefinitions>"
          "<ModelVariables><ScalarVariable name=\"x\"><Enumeration min=\"1\" max=\"2\"/></ScalarVariable></ModelVariables></fmiModelDescription>");
    std::vector<unit_definition> units(2);
    units[0].name = "u";
    units[1].name = "opaque";
    fmi::fmi2_enumeration_restrictions restrictions;
    fmi::read_xml_metadata(units, path, &restrictions);
    BOOST_TEST(units[0].has_si_base_unit);
    BOOST_TEST(!units[1].has_si_base_unit);
    BOOST_REQUIRE(restrictions.at("x").min);
    BOOST_REQUIRE(restrictions.at("x").max);
    BOOST_TEST(*restrictions.at("x").min == 1);
    BOOST_TEST(*restrictions.at("x").max == 2);

    // Models without Enumeration variables still retain their unit metadata.
    write("<fmiModelDescription><UnitDefinitions><Unit name=\"u\"><BaseUnit/></Unit></UnitDefinitions></fmiModelDescription>");
    std::vector<unit_definition> unitsWithoutEnumeration(1);
    unitsWithoutEnumeration[0].name = "u";
    fmi::fmi2_enumeration_restrictions emptyRestrictions;
    fmi::read_xml_metadata(unitsWithoutEnumeration, path, &emptyRestrictions);
    BOOST_TEST(unitsWithoutEnumeration[0].has_si_base_unit);
    BOOST_TEST(emptyRestrictions.empty());
}

// Every retained alias constrains the shared value regardless of declaration
// order, and unknown references or undeclared items must not be accepted.
BOOST_AUTO_TEST_CASE(v2_enumeration_retained_alias_validation)
{
    model_description model;
    variable_type_definition definition{};
    definition.name = "Option";
    definition.type = variable_type::enumeration;
    definition.enumeration_items = {{"One", 1, {}}, {"Two", 2, {}}, {"Three", 3, {}}};
    model.type_definitions.push_back(definition);
    variable_description first{};
    first.name = "first";
    first.type = variable_type::enumeration;
    first.reference = 9;
    first.declared_type = "Option";
    auto strict = first;
    strict.name = "strict";
    strict.max = enumeration_value{2};
    model.variables = {first, strict};
    const auto validate = [&model](int value) {
        fmi::detail::validate_enumeration_reference_value(model, 9, enumeration_value{value}, "fixture");
    };
    BOOST_CHECK_NO_THROW(validate(2));
    BOOST_CHECK_THROW(validate(3), cosim::error);
    std::swap(model.variables[0], model.variables[1]);
    BOOST_CHECK_THROW(validate(3), cosim::error);
    BOOST_CHECK_THROW(validate(4), cosim::error);
    BOOST_CHECK_THROW(
        fmi::detail::validate_enumeration_reference_value(model, 10, enumeration_value{1}, "fixture"),
        cosim::error);
}

// Missing bounds impose no extra interval, but declared items still limit
// values; a one-sided bound must not accidentally require the other side.
BOOST_AUTO_TEST_CASE(legacy_enumeration_optional_bounds)
{
    model_description model;
    variable_type_definition definition{};
    definition.name = "Option";
    definition.type = variable_type::enumeration;
    definition.enumeration_items = {{"One", 1, {}}, {"Two", 2, {}}, {"Three", 3, {}}};
    model.type_definitions.push_back(definition);
    variable_description variable{};
    variable.name = "mode";
    variable.reference = 9;
    variable.type = variable_type::enumeration;
    variable.declared_type = "Option";
    model.variables.push_back(variable);
    const auto validate = [&model](int value) {
        fmi::detail::validate_legacy_enumeration_assignment<int>(
            model, 9, enumeration_value{value}, "fixture", "2.0");
    };
    BOOST_CHECK_NO_THROW(validate(1));
    BOOST_CHECK_NO_THROW(validate(3));
    BOOST_CHECK_THROW(validate(4), cosim::error);
    model.variables[0].min = enumeration_value{2};
    BOOST_CHECK_THROW(validate(1), cosim::error);
    BOOST_CHECK_NO_THROW(validate(3));
    model.variables[0].min.reset();
    model.variables[0].max = enumeration_value{2};
    BOOST_CHECK_NO_THROW(validate(1));
    BOOST_CHECK_THROW(validate(3), cosim::error);
}

// Exercise retained aliases through FMI Library and both typed and generic
// APIs, including unchanged outputs when a later getter value is invalid.
BOOST_AUTO_TEST_CASE(v2_enumeration_alias_fmu)
{
    utility::temp_dir directory;
    utility::zip::archive(mixed_types_fmu()).extract_all(directory.path());
    const auto path = directory.path() / "modelDescription.xml";
    std::ifstream input(path);
    BOOST_REQUIRE(input);
    std::string xml((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    input.close();
    const auto position = xml.find("</ModelVariables>");
    BOOST_REQUIRE(position != std::string::npos);
    xml.insert(position, R"(
        <ScalarVariable name="signed_enumeration_input_alias" valueReference="9"
                causality="local" variability="discrete" initial="calculated">
            <Enumeration declaredType="SignedOption" max="2"/>
        </ScalarVariable>
    )");
    std::ofstream output(path);
    BOOST_REQUIRE(output);
    output << xml;
    output.close();

    auto importer = fmi::importer::create();
    auto fmu = std::static_pointer_cast<fmi::v2::fmu>(importer->import_unpacked(directory.path()));
    const auto description = fmu->model_description();
    const auto aliases = std::count_if(
        description->variables.begin(), description->variables.end(), [](const auto& variable) {
            return variable.type == variable_type::enumeration && variable.reference == 9;
        });
    BOOST_REQUIRE_EQUAL(aliases, 2);
    const auto strict = std::find_if(
        description->variables.begin(), description->variables.end(), [](const auto& variable) {
            return variable.name == "signed_enumeration_input_alias";
        });
    BOOST_REQUIRE(strict != description->variables.end());
    BOOST_REQUIRE(strict->max);
    BOOST_TEST(std::get<enumeration_value>(*strict->max).value == 2);

    auto instance = fmu->instantiate_v2_slave("alias-test");
    instance->setup(to_time_point(0.0), to_time_point(1.0), std::nullopt);
    const value_reference reference = 9;
    const enumeration_value valid{2};
    const enumeration_value invalid{(std::numeric_limits<std::int32_t>::max)()};
    instance->set_enumeration_variables(gsl::make_span(&reference, 1), gsl::make_span(&valid, 1));
    BOOST_CHECK_THROW(
        instance->set_enumeration_variables(gsl::make_span(&reference, 1), gsl::make_span(&invalid, 1)),
        cosim::error);
    const typed_value_reference generic{variable_type::enumeration, reference};
    const variable_value invalidGeneric(enumeration_values{{invalid}});
    BOOST_CHECK_THROW(instance->set_variable_values(gsl::make_span(&generic, 1), gsl::make_span(&invalidGeneric, 1)), cosim::error);
    const typed_value_reference batchReferences[] = {{variable_type::integer, 3}, generic};
    const variable_value batchValues[] = {variable_value(integer_values{{44}}), invalidGeneric};
    BOOST_CHECK_THROW(instance->set_variable_values(batchReferences, batchValues), cosim::error);
    const value_reference integerReference = 3;
    int integerValue = -1;
    instance->get_integer_variables(gsl::make_span(&integerReference, 1), gsl::make_span(&integerValue, 1));
    BOOST_TEST(integerValue == 0);
    enumeration_value observed{0};
    instance->get_enumeration_variables(gsl::make_span(&reference, 1), gsl::make_span(&observed, 1));
    BOOST_TEST(observed.value == valid.value);

    const int rawInvalid = (std::numeric_limits<int>::max)();
    instance->set_integer_variables(gsl::make_span(&reference, 1), gsl::make_span(&rawInvalid, 1));
    const value_reference references[] = {7, 9};
    enumeration_value unchanged[] = {{-10}, {-20}};
    BOOST_CHECK_THROW(instance->get_enumeration_variables(references, unchanged), cosim::error);
    BOOST_TEST(unchanged[0].value == -10);
    BOOST_TEST(unchanged[1].value == -20);
    BOOST_CHECK_THROW(instance->get_variable_values(gsl::make_span(&generic, 1)), cosim::error);
}
