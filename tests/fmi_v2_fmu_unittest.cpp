#define BOOST_TEST_MODULE cosim::fmi::v2::fmu unittest
#include <cosim/exception.hpp>
#include <cosim/fmi/importer.hpp>
#include <cosim/fmi/v2/fmu.hpp>
#include <cosim/fs_portability.hpp>
#include <cosim/algorithm/fixed_step_algorithm.hpp>
#include <cosim/observer/last_value_observer.hpp>
#include <cosim/orchestration.hpp>
#include <cosim/osp_config_parser.hpp>
#include <cosim/utility/filesystem.hpp>
#include <cosim/utility/zip.hpp>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>


using namespace cosim;

BOOST_TEST_DONT_PRINT_LOG_VALUE(fmi::fmi_version)
BOOST_TEST_DONT_PRINT_LOG_VALUE(variable_initial)

static_assert(variable_type::float64 == variable_type::real);
static_assert(variable_type::int32 == variable_type::integer);
static_assert(static_cast<int>(variable_type::boolean) == 2);
static_assert(to_text(variable_type::float64)[0] == 'r');
static_assert(to_text(variable_type::int32)[0] == 'i');
static_assert(to_text(static_cast<variable_type>(-1))[0] == 'N');

namespace
{
cosim::filesystem::path reference_fmu_directory()
{
    const auto* referenceDir = std::getenv("REFERENCE_FMU_V2_DIR");
    if (referenceDir == nullptr) {
        throw std::runtime_error(
            "REFERENCE_FMU_V2_DIR must identify the generated FMI 2 FMUs");
    }
    return referenceDir;
}


cosim::filesystem::path mixed_types_fmu()
{
    const auto* path = std::getenv("FMI2_MIXED_TYPES_FMU");
    if (path == nullptr) {
        throw std::runtime_error(
            "FMI2_MIXED_TYPES_FMU must identify the generated mixed-types FMU");
    }
    return path;
}


std::shared_ptr<fmi::v2::slave_instance> instantiate_derivative_fixture(
    const std::string& instanceName)
{
    auto importer = fmi::importer::create();
    auto fmu = std::static_pointer_cast<fmi::v2::fmu>(
        importer->import(reference_fmu_directory() / "Fmi2Derivatives.fmu"));
    return fmu->instantiate_v2_slave(instanceName);
}
} // namespace

#include "fmi2_review_metadata_tests.hpp"
#include "fmi12_batch_validation_tests.hpp"

// FMI 2 validates the complete generic batch before writing, but cannot undo
// an earlier successful FMU call if a later setter itself returns an error.
BOOST_AUTO_TEST_CASE(v2_generic_batch_prevalidation_and_fmu_failure)
{
    auto importer = fmi::importer::create();
    auto fmu = std::static_pointer_cast<fmi::v2::fmu>(importer->import(mixed_types_fmu()));
    auto instance = fmu->instantiate_v2_slave("v2-batch");
    instance->setup(to_time_point(0), to_time_point(1), std::nullopt);
    check_legacy_batch_prevalidation(*instance, 3, 7);
    // Status value 9 passes generic type/reference checks but the fixture's
    // setter rejects it, exposing the limit of batch prevalidation.
    const typed_value_reference references[] = {{variable_type::integer, 3}, {variable_type::integer, 11}};
    const variable_value values[] = {variable_value(integer_values{{44}}), variable_value(integer_values{{9}})};
    BOOST_CHECK_THROW(instance->set_variable_values(references, values), cosim::error);
    const value_reference integerReference = 3;
    int observed = 0;
    instance->get_integer_variables(gsl::make_span(&integerReference, 1), gsl::make_span(&observed, 1));
    BOOST_TEST(observed == 44);
}


// Mixed FMI 2 scalar writes must retain their dispatch order and values after
// prevalidation; Enumeration remains on its separately validated path.
BOOST_AUTO_TEST_CASE(v2_generic_batch_mixed_scalar_dispatch)
{
    auto importer = fmi::importer::create();
    auto fmu = std::static_pointer_cast<fmi::v2::fmu>(importer->import(mixed_types_fmu()));
    auto instance = fmu->instantiate_v2_slave("v2-mixed-batch");
    instance->setup(to_time_point(0), to_time_point(1), std::nullopt);
    const typed_value_reference references[] = {
        {variable_type::real, 1}, {variable_type::integer, 3},
        {variable_type::boolean, 5}, {variable_type::enumeration, 7}};
    const variable_value values[] = {
        variable_value(real_values{{2.5}}), variable_value(integer_values{{7}}),
        variable_value(boolean_values{{true}}),
        variable_value(enumeration_values{{enumeration_value{2}}})};
    instance->set_variable_values(references, values);
    const auto result = instance->get_variable_values(references);
    BOOST_REQUIRE_EQUAL(result.size(), 4U);
    for (std::size_t i = 0; i < result.size(); ++i) {
        BOOST_CHECK(result[i] == values[i]);
    }
}

// The generated FMI 2 Feedthrough has a String input absent from the mixed
// fixture, so exercise its generic dispatch and copied getter result as well.
BOOST_AUTO_TEST_CASE(v2_generic_batch_string_dispatch)
{
    auto importer = fmi::importer::create();
    auto fmu = std::static_pointer_cast<fmi::v2::fmu>(
        importer->import(reference_fmu_directory() / "Feedthrough.fmu"));
    const auto model = fmu->model_description();
    const auto input = std::find_if(
        model->variables.begin(), model->variables.end(), [](const auto& variable) {
            return variable.name == "String_input" && variable.type == variable_type::string;
        });
    BOOST_REQUIRE(input != model->variables.end());
    auto instance = fmu->instantiate_v2_slave("v2-string-batch");
    instance->setup(to_time_point(0), to_time_point(1), std::nullopt);
    const typed_value_reference reference{variable_type::string, input->reference};
    const variable_value value(string_values{{"batch string"}});
    instance->set_variable_values(gsl::make_span(&reference, 1), gsl::make_span(&value, 1));
    const auto result = instance->get_variable_values(gsl::make_span(&reference, 1));
    BOOST_REQUIRE_EQUAL(result.size(), 1U);
    BOOST_CHECK(result[0] == value);
}

BOOST_AUTO_TEST_CASE(model_metadata_enum_text)
{
    const std::array<std::pair<variable_type, const char*>, 15> types{{
        {variable_type::real, "real"},
        {variable_type::integer, "integer"},
        {variable_type::boolean, "boolean"},
        {variable_type::string, "string"},
        {variable_type::enumeration, "enumeration"},
        {variable_type::float32, "float32"},
        {variable_type::int8, "int8"},
        {variable_type::uint8, "uint8"},
        {variable_type::int16, "int16"},
        {variable_type::uint16, "uint16"},
        {variable_type::uint32, "uint32"},
        {variable_type::int64, "int64"},
        {variable_type::uint64, "uint64"},
        {variable_type::binary, "binary"},
        {variable_type::clock, "clock"},
    }};
    for (const auto& [type, text] : types) {
        BOOST_TEST(std::string(to_text(type)) == text);
    }
    BOOST_TEST(std::string(to_text(variable_type::float64)) == "real");
    BOOST_TEST(std::string(to_text(variable_type::int32)) == "integer");
    BOOST_TEST(std::string(to_text(static_cast<variable_type>(-1))) == "NULL");
    BOOST_TEST(std::string(to_text(static_cast<variable_causality>(7))) == "NULL");
    BOOST_TEST(std::string(to_text(static_cast<variable_variability>(7))) == "NULL");
}


BOOST_AUTO_TEST_CASE(v2_fmu)
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_TEST_REQUIRE(!!testDataDir);
    auto importer = fmi::importer::create();
    auto fmu = importer->import(
        cosim::filesystem::path(testDataDir) / "fmi2" / "vector.fmu");

    BOOST_TEST(fmu->fmi_version() == fmi::fmi_version::v2_0);
    const auto d = fmu->model_description();
    BOOST_TEST(d->name == "com.open-simulation-platform.vector");
    BOOST_TEST(d->uuid == "b928fd8c-743f-50d0-b4bb-98bc5a5f804e");
    BOOST_TEST(d->description == "Has one input and one input, both 3D vectors. Copies the input to the output each step.");
    BOOST_TEST(d->author == "SINTEF Ocean & DNV GL");
    BOOST_TEST(d->version == "0.1");
    BOOST_TEST(std::static_pointer_cast<fmi::v2::fmu>(fmu)->fmilib_handle() != nullptr);
    BOOST_TEST(cosim::filesystem::exists(
        std::static_pointer_cast<fmi::v2::fmu>(fmu)->directory() / "modelDescription.xml"));

    auto instance = fmu->instantiate_slave("testSlave");
    instance->setup(
        cosim::to_time_point(0.0),
        cosim::to_time_point(1.0),
        std::nullopt);

    bool foundInput0 = false;
    bool foundOutput1 = false;
    for (const auto& v : d->variables) {
        if (v.name == "input[0]") {
            foundInput0 = true;
            BOOST_TEST(v.type == variable_type::real);
            BOOST_TEST(v.type == variable_type::float64);
            BOOST_TEST(std::string(to_text(v.type)) == "real");
            BOOST_TEST(v.variability == variable_variability::discrete);
            BOOST_TEST(v.causality == variable_causality::input);
            double start = std::get<double>(*v.start);
            BOOST_TEST(start == 0.0);
            double varVal = -1.0;
            instance->get_real_variables(
                gsl::make_span(&v.reference, 1), gsl::make_span(&varVal, 1));
            BOOST_TEST(varVal == 0.0);
        } else if (v.name == "output[1]") {
            foundOutput1 = true;
            BOOST_TEST(v.type == variable_type::real);
            BOOST_TEST(v.variability == variable_variability::discrete);
            BOOST_TEST(v.causality == variable_causality::output);
            BOOST_TEST(!v.start.has_value());
        }
    }
    BOOST_TEST(foundInput0);
    BOOST_TEST(foundOutput1);
}


BOOST_AUTO_TEST_CASE(v2_enumeration)
{
    auto importer = fmi::importer::create();
    auto fmu = std::static_pointer_cast<fmi::v2::fmu>(
        importer->import(reference_fmu_directory() / "Feedthrough.fmu"));
    const auto description = fmu->model_description();

    const auto typeDefinition = std::find_if(
        description->type_definitions.begin(),
        description->type_definitions.end(),
        [](const auto& definition) { return definition.name == "Option"; });
    BOOST_REQUIRE(typeDefinition != description->type_definitions.end());
    BOOST_TEST(typeDefinition->type == variable_type::enumeration);
    BOOST_REQUIRE_EQUAL(typeDefinition->enumeration_items.size(), 2U);
    BOOST_TEST(typeDefinition->enumeration_items[0].name == "Option 1");
    BOOST_TEST(typeDefinition->enumeration_items[0].value == 1);
    BOOST_TEST(
        typeDefinition->enumeration_items[0].description == "First option");
    BOOST_TEST(typeDefinition->enumeration_items[1].name == "Option 2");
    BOOST_TEST(typeDefinition->enumeration_items[1].value == 2);
    BOOST_TEST(
        typeDefinition->enumeration_items[1].description == "Second option");

    const auto input = std::find_if(
        description->variables.begin(),
        description->variables.end(),
        [](const auto& variable) {
            return variable.name == "Enumeration_input";
        });
    const auto output = std::find_if(
        description->variables.begin(),
        description->variables.end(),
        [](const auto& variable) {
            return variable.name == "Enumeration_output";
        });
    BOOST_REQUIRE(input != description->variables.end());
    BOOST_REQUIRE(output != description->variables.end());
    BOOST_TEST(input->type == variable_type::enumeration);
    BOOST_TEST(output->type == variable_type::enumeration);
    BOOST_REQUIRE(input->declared_type.has_value());
    BOOST_TEST(*input->declared_type == "Option");
    BOOST_REQUIRE(input->start.has_value());
    BOOST_TEST(std::get<enumeration_value>(*input->start).value == 1);

    auto instance = fmu->instantiate_v2_slave("fmi2-enumeration");
    instance->setup(to_time_point(0.0), to_time_point(2.0), std::nullopt);

    const auto unsupported = [](const cosim::error& error) {
        return error.code() == make_error_code(errc::unsupported_feature);
    };
    for (const auto type : {variable_type::float32,
             variable_type::uint64,
             variable_type::clock,
             static_cast<variable_type>(-1)}) {
        const typed_value_reference variable{type, 0};
        BOOST_CHECK_EXCEPTION(
            instance->get_variable_values(gsl::make_span(&variable, 1)),
            cosim::error,
            unsupported);
    }
    const typed_value_reference unsupportedInput{variable_type::float32, 0};
    const variable_value unsupportedValue(float32_values{{1.0F}});
    BOOST_CHECK_EXCEPTION(
        instance->set_variable_values(
            gsl::make_span(&unsupportedInput, 1),
            gsl::make_span(&unsupportedValue, 1)),
        cosim::error,
        unsupported);

    enumeration_value inputValue{2};
    instance->set_enumeration_variables(
        gsl::make_span(&input->reference, 1),
        gsl::make_span(&inputValue, 1));
    const typed_value_reference genericInput{
        variable_type::enumeration, input->reference};
    const variable_value genericInputValue(enumeration_values{{inputValue}});
    instance->set_variable_values(
        gsl::make_span(&genericInput, 1),
        gsl::make_span(&genericInputValue, 1));
    const variable_value integerEnumValue(integer_values{{2}});
    instance->set_variable_values(
        gsl::make_span(&genericInput, 1),
        gsl::make_span(&integerEnumValue, 1));

    enumeration_value invalidValue{3};
    BOOST_CHECK_EXCEPTION(
        instance->set_enumeration_variables(
            gsl::make_span(&input->reference, 1),
            gsl::make_span(&invalidValue, 1)),
        cosim::error,
        [](const cosim::error& error) {
            return error.code() == make_error_code(errc::model_error);
        });

    instance->start_simulation();
    BOOST_CHECK(
        static_cast<int>(
            instance->do_step(to_time_point(0.0), to_duration(1.0))) ==
        static_cast<int>(step_result::complete));

    enumeration_value outputValue;
    instance->get_enumeration_variables(
        gsl::make_span(&output->reference, 1),
        gsl::make_span(&outputValue, 1));
    BOOST_TEST(outputValue.value == inputValue.value);

    const typed_value_reference genericOutput{
        variable_type::enumeration, output->reference};
    const auto genericOutputValues =
        instance->get_variable_values(gsl::make_span(&genericOutput, 1));
    BOOST_REQUIRE_EQUAL(genericOutputValues.size(), 1U);
    BOOST_TEST(
        std::get<enumeration_values>(genericOutputValues[0].values())
            .values[0]
            .value == inputValue.value);
    instance->end_simulation();
}

BOOST_AUTO_TEST_CASE(v2_enumeration_osp_integer_initial_value)
{
    utility::temp_dir directory;
    filesystem::copy_file(mixed_types_fmu(), directory.path() / "MixedTypes.fmu");
    const auto configPath = directory.path() / "OspSystemStructure.xml";
    {
        std::ofstream config(configPath);
        config << R"(<?xml version="1.0" encoding="UTF-8"?>
<OspSystemStructure xmlns="http://opensimulationplatform.com/MSMI/OSPSystemStructure" version="0.1">
  <StartTime>0.0</StartTime><BaseStepSize>0.1</BaseStepSize><Algorithm>fixedStep</Algorithm>
  <Simulators><Simulator name="fmi2" source="MixedTypes.fmu"><InitialValues>
    <InitialValue variable="enumeration_input"><Integer value="2"/></InitialValue>
  </InitialValues></Simulator></Simulators><Connections/>
</OspSystemStructure>)";
        BOOST_REQUIRE(config.good());
    }
    auto resolver = default_model_uri_resolver();
    const auto config = load_osp_config(configPath, *resolver);
    const auto& initial = config.initial_values.at({"fmi2", "enumeration_input"});
    BOOST_TEST(type_of(initial) == variable_type::enumeration);
    BOOST_TEST(std::get<enumeration_value>(initial).value == 2);
    auto execution = cosim::execution(
        config.start_time,
        std::make_shared<fixed_step_algorithm>(
            std::get<fixed_step_algorithm_params>(config.algorithm_configuration)));
    auto observer = std::make_shared<last_value_observer>();
    execution.add_observer(observer);
    const auto entities = inject_system_structure(
        execution, config.system_structure, config.initial_values);
    const auto simulator = entities.simulators.at("fmi2");
    const auto output = find_variable(
        execution.get_model_description(simulator), "enumeration_output");
    BOOST_REQUIRE(output);
    BOOST_REQUIRE(execution.simulate_until(to_time_point(0.2)));
    const auto value = observer->get_variable_value(
        simulator, variable_type::enumeration, output->reference);
    BOOST_TEST(std::get<enumeration_values>(value.values()).values[0].value == 2);
}


BOOST_AUTO_TEST_CASE(v2_signed_enumeration_fixture)
{
    auto importer = fmi::importer::create();
    auto fmu = std::static_pointer_cast<fmi::v2::fmu>(
        importer->import(mixed_types_fmu()));
    const auto description = fmu->model_description();

    const auto typeDefinition = std::find_if(
        description->type_definitions.begin(),
        description->type_definitions.end(),
        [](const auto& definition) {
            return definition.name == "SignedOption";
        });
    BOOST_REQUIRE(typeDefinition != description->type_definitions.end());
    BOOST_TEST(typeDefinition->type == variable_type::enumeration);
    BOOST_REQUIRE_EQUAL(typeDefinition->enumeration_items.size(), 5U);
    const auto findItem = [&typeDefinition](const std::string& name) {
        return std::find_if(
            typeDefinition->enumeration_items.begin(),
            typeDefinition->enumeration_items.end(),
            [&name](const auto& item) { return item.name == name; });
    };
    const auto negative = findItem("Negative");
    const auto zero = findItem("Zero");
    const auto one = findItem("One");
    const auto two = findItem("Two");
    const auto positive = findItem("Positive");
    BOOST_REQUIRE(negative != typeDefinition->enumeration_items.end());
    BOOST_REQUIRE(zero != typeDefinition->enumeration_items.end());
    BOOST_REQUIRE(one != typeDefinition->enumeration_items.end());
    BOOST_REQUIRE(two != typeDefinition->enumeration_items.end());
    BOOST_REQUIRE(positive != typeDefinition->enumeration_items.end());
    BOOST_TEST(
        negative->value == (std::numeric_limits<std::int32_t>::min)());
    BOOST_TEST(zero->value == 0);
    BOOST_TEST(one->value == 1);
    BOOST_TEST(two->value == 2);
    BOOST_TEST(
        positive->value == (std::numeric_limits<std::int32_t>::max)());
    BOOST_TEST(negative->description == "Minimum FMI 2 Integer value");

    const auto input = std::find_if(
        description->variables.begin(),
        description->variables.end(),
        [](const auto& variable) {
            return variable.name == "signed_enumeration_input";
        });
    const auto output = std::find_if(
        description->variables.begin(),
        description->variables.end(),
        [](const auto& variable) {
            return variable.name == "signed_enumeration_output";
        });
    BOOST_REQUIRE(input != description->variables.end());
    BOOST_REQUIRE(output != description->variables.end());
    BOOST_TEST(input->type == variable_type::enumeration);
    BOOST_TEST(output->type == variable_type::enumeration);
    BOOST_REQUIRE(input->declared_type.has_value());
    BOOST_TEST(*input->declared_type == "SignedOption");
    BOOST_REQUIRE(input->start.has_value());
    BOOST_TEST(
        std::get<enumeration_value>(*input->start).value ==
        (std::numeric_limits<std::int32_t>::min)());

    auto instance = fmu->instantiate_v2_slave("fmi2-signed-enumeration");
    instance->setup(to_time_point(0.0), to_time_point(1.0), std::nullopt);

    const enumeration_value minimum{
        (std::numeric_limits<std::int32_t>::min)()};
    const enumeration_value maximum{
        (std::numeric_limits<std::int32_t>::max)()};
    instance->set_enumeration_variables(
        gsl::make_span(&input->reference, 1),
        gsl::make_span(&minimum, 1));
    enumeration_value observed;
    instance->get_enumeration_variables(
        gsl::make_span(&input->reference, 1),
        gsl::make_span(&observed, 1));
    BOOST_TEST(observed.value == minimum.value);

    const typed_value_reference genericInput{
        variable_type::enumeration, input->reference};
    const variable_value genericInputValue(enumeration_values{{maximum}});
    instance->set_variable_values(
        gsl::make_span(&genericInput, 1),
        gsl::make_span(&genericInputValue, 1));

    const enumeration_value undeclared{3};
    BOOST_CHECK_EXCEPTION(
        instance->set_enumeration_variables(
            gsl::make_span(&input->reference, 1),
            gsl::make_span(&undeclared, 1)),
        cosim::error,
        [](const cosim::error& error) {
            return error.code() == make_error_code(errc::model_error);
        });

    instance->start_simulation();
    BOOST_TEST(
        static_cast<int>(
            instance->do_step(to_time_point(0.0), to_duration(1.0))) ==
        static_cast<int>(step_result::complete));
    instance->get_enumeration_variables(
        gsl::make_span(&output->reference, 1),
        gsl::make_span(&observed, 1));
    BOOST_TEST(observed.value == maximum.value);

    const typed_value_reference genericOutput{
        variable_type::enumeration, output->reference};
    const auto genericOutputValues =
        instance->get_variable_values(gsl::make_span(&genericOutput, 1));
    BOOST_REQUIRE_EQUAL(genericOutputValues.size(), 1U);
    BOOST_TEST(
        std::get<enumeration_values>(genericOutputValues[0].values())
            .values[0]
            .value == maximum.value);
    instance->end_simulation();
}


BOOST_AUTO_TEST_CASE(v2_enumeration_status_contract)
{
    auto importer = fmi::importer::create();
    auto fmu = std::static_pointer_cast<fmi::v2::fmu>(
        importer->import(mixed_types_fmu()));
    auto instance = fmu->instantiate_v2_slave("fmi2-enumeration-status");
    instance->setup(to_time_point(0.0), to_time_point(1.0), std::nullopt);
    instance->start_simulation();

    const value_reference statusReference = 11;
    const value_reference outputReference = 8;
    const auto setStatus = [&](int status) {
        instance->set_integer_variables(
            gsl::make_span(&statusReference, 1),
            gsl::make_span(&status, 1));
    };

    setStatus(1);
    enumeration_value warningValue{99};
    instance->get_enumeration_variables(
        gsl::make_span(&outputReference, 1),
        gsl::make_span(&warningValue, 1));
    BOOST_TEST(warningValue.value == 1);

    for (const int status : std::array<int, 3>{2, 3, 4}) {
        setStatus(status);
        enumeration_value unchanged{99};
        BOOST_CHECK_EXCEPTION(
            instance->get_enumeration_variables(
                gsl::make_span(&outputReference, 1),
                gsl::make_span(&unchanged, 1)),
            cosim::error,
            [](const cosim::error& error) {
                return error.code() == make_error_code(errc::model_error);
            });
        BOOST_TEST(unchanged.value == 99);
    }

    instance->end_simulation();
}


BOOST_AUTO_TEST_CASE(v2_enumeration_malformed_metadata)
{
    const auto importMustFail =
        [](const std::string& typeDefinition,
           const std::string& variableDefinition) {
            const auto modelDirectory = cosim::utility::temp_dir();
            std::ofstream modelDescription(
                modelDirectory.path() / "modelDescription.xml");
            modelDescription
                << R"(<?xml version="1.0" encoding="UTF-8"?>
<fmiModelDescription
    fmiVersion="2.0"
    modelName="BadEnumeration"
    guid="{b6a8bb7d-3c77-4f0c-9c1d-4b6b1a17e0d2}"
    variableNamingConvention="flat"
    numberOfEventIndicators="0">
  <CoSimulation modelIdentifier="BadEnumeration"/>
  <TypeDefinitions>)"
                << typeDefinition
                << R"(</TypeDefinitions>
  <ModelVariables>)"
                << variableDefinition
                << R"(</ModelVariables>
  <ModelStructure><Outputs/></ModelStructure>
</fmiModelDescription>
)";
            modelDescription.close();

            auto importer = fmi::importer::create();
            BOOST_CHECK_EXCEPTION(
                importer->import_unpacked(modelDirectory.path()),
                cosim::error,
                [](const cosim::error& error) {
                    return error.code() == make_error_code(errc::bad_file);
                });
        };
    const std::string validType = R"(
    <SimpleType name="Option">
      <Enumeration>
        <Item name="One" value="1"/>
        <Item name="Two" value="2"/>
      </Enumeration>
    </SimpleType>
)";
    const std::string validVariable = R"(
    <ScalarVariable name="input" valueReference="1"
        causality="input" variability="discrete">
      <Enumeration declaredType="Option" start="1"/>
    </ScalarVariable>
)";

    importMustFail(
        validType,
        R"(
    <ScalarVariable name="input" valueReference="1"
        causality="input" variability="discrete">
      <Enumeration declaredType="Missing" start="1"/>
    </ScalarVariable>
)");
    importMustFail(
        R"(
    <SimpleType name="Option">
      <Enumeration>
        <Item name="One" value="1"/>
        <Item name="One" value="2"/>
      </Enumeration>
    </SimpleType>
)",
        validVariable);
    importMustFail(
        validType,
        R"(
    <ScalarVariable name="input" valueReference="1"
        causality="input" variability="discrete">
      <Enumeration declaredType="Option" start="3"/>
    </ScalarVariable>
)");
}


BOOST_AUTO_TEST_CASE(v2_reference_directional_derivatives)
{
    auto importer = fmi::importer::create();
    auto vanDerPol = std::static_pointer_cast<fmi::v2::fmu>(
        importer->import(reference_fmu_directory() / "VanDerPol.fmu"));
    auto instance =
        vanDerPol->instantiate_v2_slave("fmi2-reference-derivatives");

    BOOST_TEST(instance->provides_directional_derivatives());

    const value_reference unknowns[] = {2, 4};
    const value_reference initializationKnowns[] = {1, 3, 5};
    const double initializationSeed[] = {1.0, 1.0, 1.0};
    double sensitivity[] = {0.0, 0.0};

    BOOST_CHECK_EXCEPTION(
        instance->get_directional_derivative(
            unknowns,
            initializationKnowns,
            initializationSeed,
            sensitivity),
        cosim::error,
        [](const cosim::error& error) {
            return error.code() == make_error_code(errc::invalid_operation) &&
                std::string(error.what()).find(
                    "GetDirectionalDerivative can only be called in lifecycle states "
                    "initialization (1), step (2), or terminated (3); current lifecycle "
                    "state: 0") !=
                std::string::npos;
        });

    instance->setup(
        to_time_point(0.0), to_time_point(1.0), std::nullopt);
    instance->get_directional_derivative(
        unknowns,
        initializationKnowns,
        initializationSeed,
        sensitivity);
    BOOST_TEST(sensitivity[0] == 1.0);
    BOOST_TEST(sensitivity[1] == -4.0);

    instance->start_simulation();
    const value_reference stepKnowns[] = {1, 3};
    const double stepSeed[] = {2.0, -1.0};
    instance->get_directional_derivative(
        unknowns, stepKnowns, stepSeed, sensitivity);
    BOOST_TEST(sensitivity[0] == -1.0);
    BOOST_TEST(sensitivity[1] == 1.0);

    instance->end_simulation();
    instance->get_directional_derivative(
        unknowns, stepKnowns, stepSeed, sensitivity);
    BOOST_TEST(sensitivity[0] == -1.0);
    BOOST_TEST(sensitivity[1] == 1.0);

    auto unsupported = std::static_pointer_cast<fmi::v2::fmu>(
        importer->import(reference_fmu_directory() / "Feedthrough.fmu"));
    auto unsupportedInstance =
        unsupported->instantiate_v2_slave("fmi2-unsupported-derivatives");
    BOOST_TEST(!unsupportedInstance->provides_directional_derivatives());
    unsupportedInstance->setup(
        to_time_point(0.0), to_time_point(1.0), std::nullopt);
    BOOST_CHECK_EXCEPTION(
        unsupportedInstance->get_directional_derivative(
            unknowns,
            initializationKnowns,
            initializationSeed,
            sensitivity),
        cosim::error,
        [](const cosim::error& error) {
            return error.code() == make_error_code(errc::unsupported_feature);
        });
}


BOOST_AUTO_TEST_CASE(v2_directional_derivative_contract)
{
    auto instance = instantiate_derivative_fixture("fmi2-derivative-contract");
    const auto status =
        find_variable(instance->model_description(), "derivativeStatus");
    BOOST_REQUIRE(status.has_value());
    BOOST_TEST(status->type == variable_type::integer);
    BOOST_TEST(status->type == variable_type::int32);
    BOOST_TEST(std::string(to_text(status->type)) == "integer");

    instance->setup(
        to_time_point(0.0), to_time_point(1.0), std::nullopt);

    const value_reference unknowns[] = {10, 11};
    const value_reference knowns[] = {1, 2, 3};
    const double seed[] = {2.0, -1.0, 4.0};
    double sensitivity[] = {-100.0, -100.0};
    instance->get_directional_derivative(
        unknowns, knowns, seed, sensitivity);
    BOOST_TEST(sensitivity[0] == 9.0);
    BOOST_TEST(sensitivity[1] == 14.0);

    const value_reference reorderedUnknowns[] = {11, 10};
    const value_reference reorderedKnowns[] = {3, 1, 2};
    const double reorderedSeed[] = {4.0, 2.0, -1.0};
    instance->get_directional_derivative(
        reorderedUnknowns, reorderedKnowns, reorderedSeed, sensitivity);
    BOOST_TEST(sensitivity[0] == 14.0);
    BOOST_TEST(sensitivity[1] == 9.0);

    const value_reference callCountReference = 101;
    int callCount = 0;
    instance->get_integer_variables(
        gsl::make_span(&callCountReference, 1),
        gsl::make_span(&callCount, 1));
    BOOST_TEST(callCount == 2);

    BOOST_CHECK_THROW(
        instance->get_directional_derivative(
            unknowns,
            knowns,
            gsl::make_span(seed, 2),
            sensitivity),
        std::invalid_argument);
    BOOST_CHECK_THROW(
        instance->get_directional_derivative(
            unknowns,
            knowns,
            seed,
            gsl::make_span(sensitivity, 1)),
        std::invalid_argument);

    const value_reference nonReal = 102;
    BOOST_CHECK_EXCEPTION(
        instance->get_directional_derivative(
            gsl::make_span(&nonReal, 1),
            knowns,
            seed,
            gsl::make_span(sensitivity, 1)),
        cosim::error,
        [](const cosim::error& error) {
            return error.code() == make_error_code(errc::model_error);
        });
    const value_reference invalidUnknown = 1;
    BOOST_CHECK_EXCEPTION(
        instance->get_directional_derivative(
            gsl::make_span(&invalidUnknown, 1),
            knowns,
            seed,
            gsl::make_span(sensitivity, 1)),
        cosim::error,
        [](const cosim::error& error) {
            return error.code() == make_error_code(errc::model_error);
        });

    const gsl::span<const value_reference> noReferences;
    const gsl::span<const double> noSeed;
    BOOST_CHECK_THROW(
        instance->get_directional_derivative(
            noReferences, knowns, seed, sensitivity),
        std::invalid_argument);
    BOOST_CHECK_THROW(
        instance->get_directional_derivative(
            unknowns, noReferences, noSeed, sensitivity),
        std::invalid_argument);

    instance->get_integer_variables(
        gsl::make_span(&callCountReference, 1),
        gsl::make_span(&callCount, 1));
    BOOST_TEST(callCount == 2);
}


BOOST_AUTO_TEST_CASE(v2_directional_derivative_statuses)
{
    const value_reference unknowns[] = {10, 11};
    const value_reference knowns[] = {1, 2, 3};
    const double seed[] = {2.0, -1.0, 4.0};
    const value_reference statusReference = 100;

    {
        auto instance = instantiate_derivative_fixture(
            "fmi2-derivative-warning");
        instance->setup(
            to_time_point(0.0), to_time_point(1.0), std::nullopt);
        const int warning = 1;
        instance->set_integer_variables(
            gsl::make_span(&statusReference, 1),
            gsl::make_span(&warning, 1));
        double sensitivity[] = {-100.0, -100.0};
        instance->get_directional_derivative(
            unknowns, knowns, seed, sensitivity);
        BOOST_TEST(sensitivity[0] == 9.0);
        BOOST_TEST(sensitivity[1] == 14.0);
    }

    for (const int failureStatus : std::array<int, 4>{2, 3, 4, 5}) {
        auto instance = instantiate_derivative_fixture(
            "fmi2-derivative-failure-" + std::to_string(failureStatus));
        instance->setup(
            to_time_point(0.0), to_time_point(1.0), std::nullopt);
        instance->set_integer_variables(
            gsl::make_span(&statusReference, 1),
            gsl::make_span(&failureStatus, 1));
        double sensitivity[] = {-100.0, -100.0};
        BOOST_CHECK_EXCEPTION(
            instance->get_directional_derivative(
                unknowns, knowns, seed, sensitivity),
            cosim::error,
            [](const cosim::error& error) {
                return error.code() == make_error_code(errc::model_error);
            });
        BOOST_TEST(sensitivity[0] == -100.0);
        BOOST_TEST(sensitivity[1] == -100.0);
    }
}

BOOST_AUTO_TEST_CASE(v2_effective_metadata)
{
    utility::temp_dir directory;
    utility::zip::archive(mixed_types_fmu()).extract_all(directory.path());
    const auto path = directory.path() / "modelDescription.xml";
    std::ifstream input(path);
    BOOST_REQUIRE(input);
    std::string xml(
        (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    input.close();
    const auto replace = [&](const std::string& before, const std::string& after) {
        const auto position = xml.find(before);
        BOOST_REQUIRE(position != std::string::npos);
        xml.replace(position, before.size(), after);
    };
    replace("<TypeDefinitions>", R"(<UnitDefinitions>
        <Unit name="force">
            <BaseUnit kg="1" m="2" s="-2" factor="2" offset="3"/>
            <DisplayUnit name="displayForce" factor="4" offset="5"/>
        </Unit>
        <Unit name="dimensionless"><BaseUnit/></Unit>
        <Unit name="opaque"/>
    </UnitDefinitions>
    <TypeDefinitions>
        <SimpleType name="Force" description="force type">
            <Real quantity="Force" unit="force" displayUnit="displayForce"
                  min="-10" max="20" nominal="2" relativeQuantity="true"/>
        </SimpleType>
        <SimpleType name="Count">
            <Integer quantity="Count" min="-50" max="50"/>
        </SimpleType>)");
    replace("name=\"real_input\"", "name=\"real_input\" description=\"real input\"");
    replace("<Real start=\"0.0\"/>",
        "<Real declaredType=\"Force\" max=\"25\" start=\"0.0\"/>");
    replace("<Integer start=\"0\"/>",
        "<Integer declaredType=\"Count\" min=\"-40\" start=\"0\"/>");
    replace("declaredType=\"SignedOption\"/>",
        "declaredType=\"SignedOption\" min=\"-2147483648\" max=\"2\"/>");
    replace("</ModelVariables>", R"(<ScalarVariable name="state"
            valueReference="55" causality="local" variability="continuous">
            <Real start="1"/>
        </ScalarVariable>
        <ScalarVariable name="state_derivative"
            valueReference="56" causality="local" variability="continuous"
            initial="calculated">
            <Real derivative="12"/>
        </ScalarVariable>
        <ScalarVariable name="time"
            valueReference="57" causality="independent" variability="continuous">
            <Real/>
        </ScalarVariable>
    </ModelVariables>)");
    replace("</ModelStructure>", R"(<Derivatives><Unknown index="13"/></Derivatives>
        <InitialUnknowns><Unknown index="13"/></InitialUnknowns>
    </ModelStructure>)");
    std::ofstream output(path, std::ios::trunc);
    BOOST_REQUIRE(output);
    output << xml;
    output.close();

    auto importer = fmi::importer::create();
    const auto model = importer->import_unpacked(directory.path())->model_description();
    const auto variable = [&](const std::string& name) {
        return std::find_if(model->variables.begin(), model->variables.end(),
            [&](const auto& candidate) { return candidate.name == name; });
    };
    const auto real = variable("real_input");
    BOOST_REQUIRE(real != model->variables.end());
    BOOST_TEST(real->causality == variable_causality::input);
    BOOST_TEST(real->description == "real input");
    BOOST_REQUIRE(real->declared_type);
    BOOST_TEST(*real->declared_type == "Force");
    BOOST_REQUIRE(real->quantity);
    BOOST_TEST(*real->quantity == "Force");
    BOOST_REQUIRE(real->unit);
    BOOST_TEST(*real->unit == "force");
    BOOST_REQUIRE(real->display_unit);
    BOOST_TEST(*real->display_unit == "displayForce");
    BOOST_REQUIRE(real->min);
    BOOST_REQUIRE(real->max);
    BOOST_REQUIRE(real->nominal);
    BOOST_TEST(std::get<double>(*real->min) == -10.0);
    BOOST_TEST(std::get<double>(*real->max) == 25.0);
    BOOST_TEST(std::get<double>(*real->nominal) == 2.0);
    BOOST_REQUIRE(real->relative_quantity);
    BOOST_TEST(*real->relative_quantity);
    BOOST_TEST(!real->initial);
    BOOST_TEST(!real->reinit);

    const auto integer = variable("integer_input");
    BOOST_REQUIRE(integer != model->variables.end());
    BOOST_REQUIRE(integer->min);
    BOOST_REQUIRE(integer->max);
    BOOST_TEST(std::get<int>(*integer->min) == -40);
    BOOST_TEST(std::get<int>(*integer->max) == 50);
    const auto enumeration = variable("signed_enumeration_output");
    BOOST_REQUIRE(enumeration != model->variables.end());
    BOOST_REQUIRE(enumeration->min);
    BOOST_REQUIRE(enumeration->max);
    BOOST_TEST(std::get<enumeration_value>(*enumeration->min).value ==
        (std::numeric_limits<std::int32_t>::min)());
    BOOST_TEST(std::get<enumeration_value>(*enumeration->max).value == 2);
    BOOST_REQUIRE(enumeration->initial);
    BOOST_TEST(*enumeration->initial == variable_initial::calculated);
    const auto derivative = variable("state_derivative");
    BOOST_REQUIRE(derivative != model->variables.end());
    BOOST_TEST(derivative->causality == variable_causality::local);
    BOOST_REQUIRE(derivative->derivative);
    BOOST_TEST(*derivative->derivative == 55U);
    const auto time = variable("time");
    BOOST_REQUIRE(time != model->variables.end());
    BOOST_TEST(time->causality == variable_causality::independent);
    BOOST_TEST(!time->initial);

    const auto countType = std::find_if(
        model->type_definitions.begin(), model->type_definitions.end(),
        [](const auto& candidate) { return candidate.name == "Count"; });
    BOOST_REQUIRE(countType != model->type_definitions.end());
    BOOST_REQUIRE(countType->min);
    BOOST_REQUIRE(countType->max);
    BOOST_TEST(std::get<int>(*countType->min) == -50);
    BOOST_TEST(std::get<int>(*countType->max) == 50);

    const auto unit = [&](const std::string& name) {
        return std::find_if(model->unit_definitions.begin(), model->unit_definitions.end(),
            [&](const auto& candidate) { return candidate.name == name; });
    };
    const auto force = unit("force");
    const auto dimensionless = unit("dimensionless");
    const auto opaque = unit("opaque");
    BOOST_REQUIRE(force != model->unit_definitions.end());
    BOOST_REQUIRE(dimensionless != model->unit_definitions.end());
    BOOST_REQUIRE(opaque != model->unit_definitions.end());
    BOOST_TEST(force->has_si_base_unit);
    BOOST_TEST(force->kilogram == 1);
    BOOST_TEST(force->metre == 2);
    BOOST_TEST(force->second == -2);
    BOOST_TEST(force->factor == 2.0);
    BOOST_TEST(force->offset == 3.0);
    BOOST_REQUIRE_EQUAL(force->display_units.size(), 1U);
    BOOST_TEST(force->display_units[0].name == "displayForce");
    BOOST_TEST(force->display_units[0].factor == 4.0);
    BOOST_TEST(force->display_units[0].offset == 5.0);
    BOOST_TEST(dimensionless->has_si_base_unit);
    BOOST_TEST(!opaque->has_si_base_unit);
}
