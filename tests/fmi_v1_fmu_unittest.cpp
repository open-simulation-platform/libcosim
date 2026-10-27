#define BOOST_TEST_MODULE cosim::fmi::v1::fmu unittest
#include <cosim/fmi/importer.hpp>
#include <cosim/fmi/v1/fmu.hpp>
#include <cosim/exception.hpp>
#include <cosim/algorithm/fixed_step_algorithm.hpp>
#include <cosim/execution.hpp>
#include <cosim/observer/last_value_observer.hpp>
#include <cosim/system_structure.hpp>
#include <cosim/utility/filesystem.hpp>
#include <cosim/utility/zip.hpp>

#include <boost/test/unit_test.hpp>

#include <cstdlib>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <limits>


using namespace cosim;

BOOST_TEST_DONT_PRINT_LOG_VALUE(fmi::fmi_version)
BOOST_TEST_DONT_PRINT_LOG_VALUE(step_result)

namespace
{
class fmu_model : public model
{
public:
    explicit fmu_model(std::shared_ptr<fmi::fmu> fmu)
        : fmu_(std::move(fmu)) {}
    std::shared_ptr<const model_description> description() const noexcept override
    {
        return fmu_->model_description();
    }
    std::shared_ptr<slave> instantiate(std::string_view name) override
    {
        return fmu_->instantiate_slave(name);
    }
private:
    std::shared_ptr<fmi::fmu> fmu_;
};

void run_tests(std::shared_ptr<fmi::fmu> fmu)
{
    BOOST_TEST(fmu->fmi_version() == fmi::fmi_version::v1_0);
    const auto d = fmu->model_description();
    BOOST_TEST(d->name == "no.viproma.demo.identity");
    BOOST_TEST(d->uuid.size() == 36U);
    BOOST_TEST(d->description ==
        "Has one input and one output of each type, and outputs are always set equal to inputs");
    BOOST_TEST(d->author == "Lars Tandle Kyllingstad");
    BOOST_TEST(d->version == "0.3");
    BOOST_TEST(std::static_pointer_cast<fmi::v1::fmu>(fmu)->fmilib_handle() != nullptr);

    cosim::value_reference
        realIn = 0,
        integerIn = 0, booleanIn = 0, stringIn = 0,
        realOut = 0, integerOut = 0, booleanOut = 0, stringOut = 0;
    for (const auto& v : d->variables) {
        if (v.name == "realIn") {
            realIn = v.reference;
        } else if (v.name == "integerIn") {
            integerIn = v.reference;
        } else if (v.name == "booleanIn") {
            booleanIn = v.reference;
        } else if (v.name == "stringIn") {
            stringIn = v.reference;
        } else if (v.name == "realOut") {
            realOut = v.reference;
        } else if (v.name == "integerOut") {
            integerOut = v.reference;
        } else if (v.name == "booleanOut") {
            booleanOut = v.reference;
        } else if (v.name == "stringOut") {
            stringOut = v.reference;
        }

        if (v.name == "realIn") {
            BOOST_TEST(v.type == variable_type::real);
            BOOST_TEST(v.variability == variable_variability::discrete);
            BOOST_TEST(v.causality == variable_causality::input);
            double start = std::get<double>(*v.start);
            BOOST_TEST(start == 0.0);
        } else if (v.name == "stringOut") {
            BOOST_TEST(v.type == variable_type::string);
            BOOST_TEST(v.variability == variable_variability::discrete);
            BOOST_TEST(v.causality == variable_causality::output);
            BOOST_TEST(!v.start.has_value());
        } else if (v.name == "booleanIn") {
            BOOST_TEST(v.type == variable_type::boolean);
            BOOST_TEST(v.variability == variable_variability::discrete);
            BOOST_TEST(v.causality == variable_causality::input);
            bool start = std::get<bool>(*v.start);
            BOOST_TEST(start == false);
        }
    }

    const auto tStart = cosim::time_point();
    const auto tMax = cosim::to_time_point(1.0);
    const auto dt = cosim::to_duration(0.1);
    double realVal = 0.0;
    int integerVal = 0;
    bool booleanVal = false;
    std::string stringVal;

    auto instance = fmu->instantiate_slave("testSlave");
    instance->setup(tStart, tMax, std::nullopt);
    instance->start_simulation();

    for (auto t = tStart; t < tMax; t += dt) {
        double getRealVal = -1.0;
        int getIntegerVal = -1;
        bool getBooleanVal = true;
        std::string getStringVal = "unexpected value";

        instance->get_real_variables(gsl::make_span(&realOut, 1), gsl::make_span(&getRealVal, 1));
        instance->get_integer_variables(gsl::make_span(&integerOut, 1), gsl::make_span(&getIntegerVal, 1));
        instance->get_boolean_variables(gsl::make_span(&booleanOut, 1), gsl::make_span(&getBooleanVal, 1));
        instance->get_string_variables(gsl::make_span(&stringOut, 1), gsl::make_span(&getStringVal, 1));

        BOOST_TEST(getRealVal == realVal);
        BOOST_TEST(getIntegerVal == integerVal);
        BOOST_TEST(getBooleanVal == booleanVal);
        BOOST_TEST(getStringVal == stringVal);

        realVal += 1.0;
        integerVal += 1;
        booleanVal = !booleanVal;
        stringVal += 'a';

        instance->set_real_variables(gsl::make_span(&realIn, 1), gsl::make_span(&realVal, 1));
        instance->set_integer_variables(gsl::make_span(&integerIn, 1), gsl::make_span(&integerVal, 1));
        instance->set_boolean_variables(gsl::make_span(&booleanIn, 1), gsl::make_span(&booleanVal, 1));
        instance->set_string_variables(gsl::make_span(&stringIn, 1), gsl::make_span(&stringVal, 1));

        BOOST_TEST(instance->do_step(t, dt) == step_result::complete);
    }

    instance->end_simulation();
}
} // namespace


BOOST_AUTO_TEST_CASE(v1_fmu)
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_TEST_REQUIRE(!!testDataDir);
    auto importer = fmi::importer::create();
    auto fmu = importer->import(
        cosim::filesystem::path(testDataDir) / "fmi1" / "identity.fmu");
    run_tests(fmu);
}


BOOST_AUTO_TEST_CASE(v1_fmu_unpacked)
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_TEST_REQUIRE(!!testDataDir);
    utility::temp_dir unpackDir;
    utility::zip::archive(
        cosim::filesystem::path(testDataDir) / "fmi1" / "identity.fmu")
        .extract_all(unpackDir.path());

    auto importer = fmi::importer::create();
    auto fmu = importer->import_unpacked(unpackDir.path());
    run_tests(fmu);
}

BOOST_AUTO_TEST_CASE(v1_enumeration)
{
    const auto* path = std::getenv("FMI1_ENUMERATION_FMU");
    BOOST_REQUIRE(path != nullptr);
    auto importer = fmi::importer::create();
    auto fmu = std::static_pointer_cast<fmi::v1::fmu>(importer->import(path));
    const auto model = fmu->model_description();
    const auto definition = std::find_if(
        model->type_definitions.begin(), model->type_definitions.end(),
        [](const auto& type) { return type.name == "Option"; });
    BOOST_REQUIRE(definition != model->type_definitions.end());
    BOOST_TEST(definition->type == variable_type::enumeration);
    BOOST_REQUIRE_EQUAL(definition->enumeration_items.size(), 2U);
    BOOST_TEST(definition->enumeration_items[0].name == "One");
    BOOST_TEST(definition->enumeration_items[0].value == 1);
    BOOST_TEST(definition->enumeration_items[1].name == "Two");
    BOOST_TEST(definition->enumeration_items[1].value == 2);

    const auto find = [&](const char* name) {
        return std::find_if(model->variables.begin(), model->variables.end(),
            [&](const auto& variable) { return variable.name == name; });
    };
    const auto input = find("enumeration_input");
    const auto output = find("enumeration_output");
    const auto outputAlias = find("enumeration_output_alias");
    const auto integer = find("integer_input");
    const auto status = find("status");
    BOOST_REQUIRE(input != model->variables.end());
    BOOST_REQUIRE(output != model->variables.end());
    BOOST_REQUIRE(outputAlias != model->variables.end());
    BOOST_REQUIRE(integer != model->variables.end());
    BOOST_REQUIRE(status != model->variables.end());
    BOOST_TEST(input->type == variable_type::enumeration);
    BOOST_REQUIRE(input->declared_type);
    BOOST_TEST(*input->declared_type == "Option");
    BOOST_REQUIRE(input->start);
    BOOST_TEST(std::get<enumeration_value>(*input->start).value == 1);
    BOOST_TEST(!output->start);
    BOOST_TEST(outputAlias->type == variable_type::enumeration);
    BOOST_TEST(outputAlias->reference == output->reference);
    BOOST_TEST(integer->type == variable_type::integer);

    auto instance = fmu->instantiate_v1_slave("v1-enumeration");
    instance->setup(to_time_point(0), to_time_point(2), std::nullopt);
    const typed_value_reference genericInput{variable_type::enumeration, input->reference};
    const variable_value integerValue(integer_values{{2}});
    instance->set_variable_values(
        gsl::make_span(&genericInput, 1), gsl::make_span(&integerValue, 1));
    enumeration_value invalid{3};
    BOOST_CHECK_THROW(instance->set_enumeration_variables(
        gsl::make_span(&input->reference, 1), gsl::make_span(&invalid, 1)), cosim::error);
    invalid.value = (std::numeric_limits<std::int64_t>::max)();
    BOOST_CHECK_THROW(instance->set_enumeration_variables(
        gsl::make_span(&input->reference, 1), gsl::make_span(&invalid, 1)), cosim::error);
    enumeration_value valid{2};
    BOOST_CHECK_THROW(instance->set_enumeration_variables(
        gsl::make_span(&integer->reference, 1), gsl::make_span(&valid, 1)), cosim::error);
    BOOST_CHECK_THROW(instance->set_enumeration_variables(
        gsl::make_span(&input->reference, 1), gsl::span<const enumeration_value>()),
        std::invalid_argument);

    int statusValue = 1;
    instance->set_integer_variables(
        gsl::make_span(&status->reference, 1), gsl::make_span(&statusValue, 1));
    instance->set_enumeration_variables(
        gsl::make_span(&input->reference, 1), gsl::make_span(&valid, 1));
    statusValue = 2;
    instance->set_integer_variables(
        gsl::make_span(&status->reference, 1), gsl::make_span(&statusValue, 1));
    BOOST_CHECK_THROW(instance->set_enumeration_variables(
        gsl::make_span(&input->reference, 1), gsl::make_span(&valid, 1)), nonfatal_bad_value);
    enumeration_value unchanged{99};
    BOOST_CHECK_THROW(instance->get_enumeration_variables(
        gsl::make_span(&input->reference, 1), gsl::make_span(&unchanged, 1)), cosim::error);
    BOOST_TEST(unchanged.value == 99);
    statusValue = 3;
    instance->set_integer_variables(
        gsl::make_span(&status->reference, 1), gsl::make_span(&statusValue, 1));
    BOOST_CHECK_THROW(instance->set_enumeration_variables(
        gsl::make_span(&input->reference, 1), gsl::make_span(&valid, 1)), cosim::error);
    BOOST_CHECK_THROW(instance->get_enumeration_variables(
        gsl::make_span(&input->reference, 1), gsl::make_span(&unchanged, 1)), cosim::error);
    BOOST_TEST(unchanged.value == 99);
    statusValue = 0;
    instance->set_integer_variables(
        gsl::make_span(&status->reference, 1), gsl::make_span(&statusValue, 1));

    int ordinary = 42;
    instance->set_integer_variables(
        gsl::make_span(&integer->reference, 1), gsl::make_span(&ordinary, 1));
    int observed = 0;
    instance->get_integer_variables(
        gsl::make_span(&integer->reference, 1), gsl::make_span(&observed, 1));
    BOOST_TEST(observed == 42);
    instance->start_simulation();
    instance->do_step(to_time_point(0), to_duration(1));
    const typed_value_reference genericOutput{variable_type::enumeration, output->reference};
    const auto result = instance->get_variable_values(gsl::make_span(&genericOutput, 1));
    BOOST_REQUIRE_EQUAL(result.size(), 1U);
    BOOST_TEST(result[0].type() == variable_type::enumeration);
    BOOST_TEST(std::get<enumeration_values>(result[0].values()).values[0].value == 2);
    instance->end_simulation();
}

BOOST_AUTO_TEST_CASE(v1_enumeration_invalid_metadata)
{
    const auto* path = std::getenv("FMI1_ENUMERATION_FMU");
    BOOST_REQUIRE(path != nullptr);
    const auto importMustFail = [&](const std::string& before,
                                    const std::string& after,
                                    errc expected) {
        utility::temp_dir directory;
        utility::zip::archive(path).extract_all(directory.path());
        const auto description = directory.path() / "modelDescription.xml";
        std::ifstream input(description);
        BOOST_REQUIRE(input);
        std::string xml(
            (std::istreambuf_iterator<char>(input)),
            std::istreambuf_iterator<char>());
        input.close();
        const auto position = xml.find(before);
        BOOST_REQUIRE(position != std::string::npos);
        xml.replace(position, before.size(), after);
        std::ofstream output(description, std::ios::trunc);
        BOOST_REQUIRE(output);
        output << xml;
        output.close();
        auto importer = fmi::importer::create();
        BOOST_CHECK_EXCEPTION(
            importer->import_unpacked(directory.path()), cosim::error,
            [expected](const cosim::error& error) {
                return error.code() == make_error_code(expected);
            });
    };
    importMustFail(
        "declaredType=\"Option\" start=\"1\"",
        "declaredType=\"Option\" start=\"3\"",
        errc::bad_file);
    importMustFail(
        "declaredType=\"Option\" start=\"1\"",
        "start=\"1\"",
        errc::bad_file);
    importMustFail(
        "alias=\"alias\"",
        "alias=\"negatedAlias\"",
        errc::unsupported_feature);
}

BOOST_AUTO_TEST_CASE(v1_v2_enumeration_execution)
{
    const auto* v1Path = std::getenv("FMI1_ENUMERATION_FMU");
    const auto* v2Path = std::getenv("FMI2_MIXED_TYPES_FMU");
    BOOST_REQUIRE(v1Path != nullptr);
    BOOST_REQUIRE(v2Path != nullptr);
    auto importer = fmi::importer::create();
    auto v1 = std::make_shared<fmu_model>(importer->import(v1Path));
    auto v2 = std::make_shared<fmu_model>(importer->import(v2Path));
    system_structure system;
    system.add_entity("source", v1);
    system.add_entity("middle", v2);
    system.add_entity("target", v1);
    system.connect_variables(
        {"source", "enumeration_output"}, {"middle", "enumeration_input"});
    system.connect_variables(
        {"middle", "enumeration_output"}, {"target", "enumeration_input"});

    variable_value_map initial;
    add_variable_value(initial, system, {"source", "enumeration_input"}, 2);
    BOOST_TEST(type_of(initial.at({"source", "enumeration_input"})) ==
        variable_type::enumeration);
    BOOST_TEST(std::get<enumeration_value>(
        initial.at({"source", "enumeration_input"})).value == 2);
    BOOST_TEST(type_of(scalar_value(2)) == variable_type::integer);
    const auto& input = system.get_variable_description(
        {"source", "enumeration_input"});
    BOOST_TEST(is_valid_variable_value(input, scalar_value(2), nullptr));
    auto arrayInput = input;
    arrayInput.dimensions = {fixed_dimension{2}};
    BOOST_TEST(!is_valid_variable_value(arrayInput, scalar_value(2), nullptr));

    // Raw maps bypass add_variable_value and still need normalization.
    initial.insert_or_assign({"source", "enumeration_input"}, scalar_value(2));
    auto execution = cosim::execution(
        time_point(), std::make_shared<fixed_step_algorithm>(to_duration(0.1)));
    auto observer = std::make_shared<last_value_observer>();
    execution.add_observer(observer);
    const auto entities = inject_system_structure(execution, system, initial);
    BOOST_CHECK(execution.simulate_until(to_time_point(0.5)));
    const auto target = entities.simulators.at("target");
    const auto targetOutput = find_variable(
        execution.get_model_description(target), "enumeration_output");
    BOOST_REQUIRE(targetOutput);
    const auto observed = observer->get_variable_value(
        target, variable_type::enumeration, targetOutput->reference);
    BOOST_TEST(observed.type() == variable_type::enumeration);
    BOOST_TEST(std::get<enumeration_values>(observed.values()).values[0].value == 2);
}
