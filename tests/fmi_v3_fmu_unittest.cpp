#define BOOST_TEST_MODULE cosim::fmi::v3::fmu unittest
#include "fmi3_test_helpers.hpp"

namespace cosim::test
{
void write_archive(const filesystem::path& path, const std::string& xml);
}

BOOST_AUTO_TEST_CASE(fmi3_opaque_tokens_remain_distinct_in_the_file_cache)
{
    const auto directory = cosim::utility::temp_dir();
    const auto importer = fmi::importer::create();
    std::vector<std::shared_ptr<fmi::fmu>> imported;
    const std::string tokens[] = {"a", "A", "A/", "A~2f", " ", " A", "A "};
    for (std::size_t i = 0; i < std::size(tokens); ++i) {
        const auto xml =
            std::string(R"(<fmiModelDescription fmiVersion="3.0" modelName="TokenFixture" instantiationToken=")")
            + tokens[i]
            + R"("><CoSimulation modelIdentifier="TokenFixture"/><ModelVariables><Float64 name="output" valueReference="0" causality="output"/></ModelVariables><ModelStructure><Output valueReference="0"/><InitialUnknown valueReference="0"/></ModelStructure></fmiModelDescription>)";
        const auto archive = directory.path() / ("token" + std::to_string(i) + ".fmu");
        cosim::test::write_archive(archive, xml);
        const auto fmu = importer->import(archive);
        BOOST_REQUIRE(fmu);
        BOOST_TEST(fmu->model_description()->instantiation_token == tokens[i]);
        for (const auto& other : imported) {
            BOOST_TEST(fmu != other);
            BOOST_TEST(
                std::static_pointer_cast<fmi::v3::fmu>(fmu)->directory() !=
                std::static_pointer_cast<fmi::v3::fmu>(other)->directory());
            BOOST_TEST(!cosim::filesystem::equivalent(
                std::static_pointer_cast<fmi::v3::fmu>(fmu)->directory(),
                std::static_pointer_cast<fmi::v3::fmu>(other)->directory()));
        }
        imported.push_back(fmu);
    }
}

BOOST_AUTO_TEST_CASE(fmi3_archive_identity_is_not_the_instantiation_token)
{
    const cosim::utility::temp_dir directory;
    const auto importer = fmi::importer::create();
    const auto archiveA = directory.path() / "a.fmu";
    const auto archiveB = directory.path() / "b.fmu";
    const auto xml = [](const char* name) {
        return std::string(R"(<fmiModelDescription fmiVersion="3.0" modelName=")") + name +
            R"(" instantiationToken="shared"><CoSimulation modelIdentifier="test"/><ModelVariables><Float64 name="output" valueReference="1" causality="output"/></ModelVariables><ModelStructure><Output valueReference="1"/><InitialUnknown valueReference="1"/></ModelStructure></fmiModelDescription>)";
    };
    cosim::test::write_archive(archiveA, xml("First"));
    cosim::test::write_archive(archiveB, xml("Second"));
    const auto first = importer->import(archiveA);
    const auto second = importer->import(archiveB);
    BOOST_TEST(first != second);
    BOOST_TEST(importer->import(archiveA) == first);
    BOOST_TEST(first->model_description()->name == "First");
    BOOST_TEST(second->model_description()->name == "Second");
}

BOOST_AUTO_TEST_CASE(fmi3_array_start_broadcast_with_a_constant_dimension)
{
    const cosim::utility::temp_dir directory;
    const auto importer = fmi::importer::create();
    const auto xml = [](const char* start) {
        return std::string(R"(<fmiModelDescription fmiVersion="3.0" modelName="ArrayStart" instantiationToken="array"><CoSimulation modelIdentifier="test"/><ModelVariables><UInt64 name="length" valueReference="1" variability="constant" start="3"/><Float64 name="input" valueReference="2" causality="input" start=")") +
            start + R"("><Dimension valueReference="1"/></Float64></ModelVariables><ModelStructure/></fmiModelDescription>)";
    };
    const auto good = directory.path() / "good.fmu";
    cosim::test::write_archive(good, xml("7"));
    const auto model = importer->import(good)->model_description();
    const auto* input = find_model_variable(*model, "input");
    BOOST_REQUIRE(input && input->array_start);
    const auto& values = std::get<real_values>(input->array_start->values()).values;
    BOOST_TEST(values == (std::vector<double>{7.0, 7.0, 7.0}), boost::test_tools::per_element());
    const auto bad = directory.path() / "bad.fmu";
    cosim::test::write_archive(bad, xml("6 7"));
    BOOST_CHECK_EXCEPTION(importer->import(bad), cosim::error,
        [](const cosim::error& e) { return e.code() == make_error_code(errc::bad_file); });
}


BOOST_AUTO_TEST_CASE(fmi3_token_does_not_collide_with_fmi2_guid)
{
    const auto* fixturePath = std::getenv("FMI2_MIXED_TYPES_FMU");
    BOOST_REQUIRE(fixturePath != nullptr);
    const auto importer = fmi::importer::create();
    const auto fmi2 = importer->import(fixturePath);
    BOOST_REQUIRE(fmi2);
    const auto guid = fmi2->model_description()->uuid;
    BOOST_REQUIRE(!guid.empty());

    const auto xml =
        std::string(R"(<fmiModelDescription fmiVersion="3.0" modelName="TokenFixture" instantiationToken=")")
        + guid
        + R"("><CoSimulation modelIdentifier="TokenFixture"/><ModelVariables><Float64 name="output" valueReference="0" causality="output"/></ModelVariables><ModelStructure><Output valueReference="0"/><InitialUnknown valueReference="0"/></ModelStructure></fmiModelDescription>)";
    const auto directory = cosim::utility::temp_dir();
    const auto archive = directory.path() / "token.fmu";
    cosim::test::write_archive(archive, xml);
    const auto fmi3 = importer->import(archive);
    BOOST_REQUIRE(fmi3);
    BOOST_TEST(fmi3->fmi_version() == fmi::fmi_version::v3_0);
    BOOST_TEST(fmi3->model_description()->instantiation_token == guid);
    BOOST_TEST(fmi3 != fmi2);
    BOOST_TEST(!cosim::filesystem::equivalent(
        std::static_pointer_cast<fmi::v3::fmu>(fmi3)->directory(),
        std::static_pointer_cast<fmi::v2::fmu>(fmi2)->directory()));
}


BOOST_AUTO_TEST_CASE(model_structure_output_dependencies_preserve_unknown_and_empty)
{
    const auto directory = cosim::utility::temp_dir();
    const auto importer = fmi::importer::create();
    const std::string fmi3xml = R"(
<fmiModelDescription fmiVersion="3.0" modelName="Dependencies3" instantiationToken="dependencies-3">
  <CoSimulation modelIdentifier="Dependencies3"/>
  <ModelVariables>
    <Float64 name="inputA" valueReference="21" causality="input" start="0"/>
    <Int32 name="inputB" valueReference="5" causality="input" start="0"/>
    <Float64 name="dependent" valueReference="11" causality="output"/>
    <Float64 name="empty" valueReference="12" causality="output"/>
    <Float64 name="unknown" valueReference="13" causality="output"/>
  </ModelVariables>
  <ModelStructure>
    <Output valueReference="11" dependencies="5 21" dependenciesKind="fixed discrete"/>
    <Output valueReference="12" dependencies=""/>
    <Output valueReference="13"/>
    <InitialUnknown valueReference="11"/><InitialUnknown valueReference="12"/><InitialUnknown valueReference="13"/>
  </ModelStructure>
</fmiModelDescription>)";
    const std::string fmi2xml = R"(
<fmiModelDescription fmiVersion="2.0" modelName="Dependencies2" guid="{dependencies-2}" numberOfEventIndicators="0">
  <CoSimulation modelIdentifier="Dependencies2"/>
  <ModelVariables>
    <ScalarVariable name="inputA" valueReference="21" causality="input" variability="continuous"><Real start="0"/></ScalarVariable>
    <ScalarVariable name="inputB" valueReference="5" causality="input" variability="discrete"><Integer start="0"/></ScalarVariable>
    <ScalarVariable name="dependent" valueReference="11" causality="output" variability="continuous" initial="calculated"><Real/></ScalarVariable>
    <ScalarVariable name="empty" valueReference="12" causality="output" variability="continuous" initial="calculated"><Real/></ScalarVariable>
    <ScalarVariable name="unknown" valueReference="13" causality="output" variability="continuous" initial="calculated"><Real/></ScalarVariable>
  </ModelVariables>
  <ModelStructure><Outputs>
    <Unknown index="+3" dependencies="2 1" dependenciesKind="fixed discrete"/>
    <Unknown index="4" dependencies=""/>
    <Unknown index="5"/>
  </Outputs><InitialUnknowns><Unknown index="3"/><Unknown index="4"/><Unknown index="5"/></InitialUnknowns></ModelStructure>
</fmiModelDescription>)";
    const std::string fmi1xml = R"(
<fmiModelDescription fmiVersion="1.0" modelName="Dependencies1" modelIdentifier="Dependencies1"
  guid="{dependencies-1}" numberOfContinuousStates="0" numberOfEventIndicators="0">
  <ModelVariables>
    <ScalarVariable name="inputA" valueReference="21" causality="input"><Real start="0"/></ScalarVariable>
    <ScalarVariable name="inputB" valueReference="5" causality="input"><Integer start="0"/></ScalarVariable>
    <ScalarVariable name="dependent" valueReference="11" causality="output"><Real/><DirectDependency><Name>inputB</Name><Name>inputA</Name></DirectDependency></ScalarVariable>
    <ScalarVariable name="empty" valueReference="12" causality="output"><Real/><DirectDependency/></ScalarVariable>
    <ScalarVariable name="unknown" valueReference="13" causality="output"><Real/></ScalarVariable>
  </ModelVariables>
  <Implementation><CoSimulation_StandAlone><Capabilities/></CoSimulation_StandAlone></Implementation>
</fmiModelDescription>)";
    for (const auto& fixture : std::array<std::pair<std::string, std::string>, 3>{{
             {"dependencies3.fmu", fmi3xml}, {"dependencies2.fmu", fmi2xml},
             {"dependencies1.fmu", fmi1xml}}}) {
        const auto archive = directory.path() / fixture.first;
        cosim::test::write_archive(archive, fixture.second);
        const auto fmu = importer->import(archive);
        BOOST_REQUIRE(fmu);
        const auto description = fmu->model_description();
        const auto* dependent = find_model_variable(*description, "dependent");
        const auto* empty = find_model_variable(*description, "empty");
        const auto* unknown = find_model_variable(*description, "unknown");
        BOOST_REQUIRE(dependent && empty && unknown);
        BOOST_REQUIRE(dependent->output_dependencies);
        BOOST_REQUIRE_EQUAL(dependent->output_dependencies->size(), 2U);
        const auto& dependencies = *dependent->output_dependencies;
        BOOST_TEST(dependencies[0].variable.reference == 5U);
        BOOST_CHECK(dependencies[0].variable.type == variable_type::integer);
        BOOST_CHECK(dependencies[0].kind == (fixture.first == "dependencies1.fmu"
            ? dependency_kind::dependent : dependency_kind::fixed));
        BOOST_TEST(dependencies[1].variable.reference == 21U);
        BOOST_CHECK(dependencies[1].variable.type == variable_type::real);
        BOOST_CHECK(dependencies[1].kind == (fixture.first == "dependencies1.fmu"
            ? dependency_kind::dependent : dependency_kind::discrete));
        BOOST_REQUIRE(empty->output_dependencies);
        BOOST_TEST(empty->output_dependencies->empty());
        BOOST_TEST(!unknown->output_dependencies.has_value());

        const auto& structure = description->structure;
        const bool isFmi3 = fixture.first == "dependencies3.fmu";
        BOOST_TEST(structure.outputs.size() == (isFmi3 ? 3U : 0U));
        BOOST_TEST(structure.initial_unknowns.size() == (isFmi3 ? 3U : 0U));
        BOOST_TEST(structure.continuous_state_derivatives.empty());
        BOOST_TEST(structure.clocked_states.empty());
        BOOST_TEST(structure.event_indicators.empty());
    }
}

BOOST_AUTO_TEST_CASE(fmi3_model_structure_exposes_every_declared_unknown_list)
{
    const auto directory = cosim::utility::temp_dir();
    const auto importer = fmi::importer::create();
    const auto makeXml = [](const std::string& token, const std::string& derivativeDependencies) {
        return R"xml(
<fmiModelDescription fmiVersion="3.0" modelName="Structure3" instantiationToken=")xml" + token + R"xml(">
  <ModelExchange modelIdentifier="Structure3"/>
  <CoSimulation modelIdentifier="Structure3" hasEventMode="true"/>
  <ModelVariables>
    <Clock name="tick" valueReference="30" causality="input" intervalVariability="triggered"/>
    <Float64 name="u" valueReference="1" causality="input" start="0"/>
    <Float64 name="x" valueReference="2" causality="local" variability="continuous" initial="exact" start="0"/>
    <Float64 name="der(x)" valueReference="3" causality="local" variability="continuous" initial="calculated" derivative="2"/>
    <Float64 name="held" valueReference="4" causality="local" variability="discrete" initial="exact" start="0" clocks="30"/>
    <Float64 name="indicator" valueReference="5" causality="local" variability="continuous" initial="calculated"/>
    <Float64 name="y" valueReference="6" causality="output" initial="calculated"/>
  </ModelVariables>
  <ModelStructure>
    <Output valueReference="6" dependencies="1 3" dependenciesKind="tunable dependent"/>
    <ContinuousStateDerivative valueReference="3" )xml" + derivativeDependencies + R"xml(/>
    <ClockedState valueReference="4" dependencies="1"/>
    <InitialUnknown valueReference="3" dependencies="2" dependenciesKind="dependent"/>
    <InitialUnknown valueReference="6"/>
    <EventIndicator valueReference="5" dependencies=""/>
  </ModelStructure>
</fmiModelDescription>)xml";
    };

    const auto archive = directory.path() / "structure3.fmu";
    cosim::test::write_archive(archive, makeXml("structure-3", R"(dependencies="1 2" dependenciesKind="dependent constant")"));
    const auto fmu = importer->import(archive);
    BOOST_REQUIRE(fmu);
    const auto& structure = fmu->model_description()->structure;

    BOOST_REQUIRE_EQUAL(structure.outputs.size(), 1U);
    BOOST_TEST(structure.outputs[0].variable.reference == 6U);
    BOOST_REQUIRE(structure.outputs[0].dependencies);
    BOOST_REQUIRE_EQUAL(structure.outputs[0].dependencies->size(), 2U);
    BOOST_CHECK((*structure.outputs[0].dependencies)[0].kind == dependency_kind::tunable);
    const auto* output = find_model_variable(*fmu->model_description(), "y");
    BOOST_REQUIRE(output && output->output_dependencies);
    BOOST_CHECK(output->output_dependencies->size() == 2U);

    BOOST_REQUIRE_EQUAL(structure.continuous_state_derivatives.size(), 1U);
    const auto& derivative = structure.continuous_state_derivatives[0];
    BOOST_TEST(derivative.variable.reference == 3U);
    BOOST_CHECK(derivative.variable.type == variable_type::real);
    BOOST_REQUIRE(derivative.dependencies);
    BOOST_REQUIRE_EQUAL(derivative.dependencies->size(), 2U);
    BOOST_TEST((*derivative.dependencies)[0].variable.reference == 1U);
    BOOST_CHECK((*derivative.dependencies)[0].kind == dependency_kind::dependent);
    BOOST_TEST((*derivative.dependencies)[1].variable.reference == 2U);
    BOOST_CHECK((*derivative.dependencies)[1].kind == dependency_kind::constant);

    BOOST_REQUIRE_EQUAL(structure.clocked_states.size(), 1U);
    BOOST_TEST(structure.clocked_states[0].variable.reference == 4U);
    BOOST_REQUIRE(structure.clocked_states[0].dependencies);
    BOOST_REQUIRE_EQUAL(structure.clocked_states[0].dependencies->size(), 1U);
    BOOST_TEST((*structure.clocked_states[0].dependencies)[0].variable.reference == 1U);

    BOOST_REQUIRE_EQUAL(structure.initial_unknowns.size(), 2U);
    BOOST_TEST(structure.initial_unknowns[0].variable.reference == 3U);
    BOOST_REQUIRE(structure.initial_unknowns[0].dependencies);
    BOOST_TEST(structure.initial_unknowns[0].dependencies->size() == 1U);
    BOOST_TEST(structure.initial_unknowns[1].variable.reference == 6U);
    BOOST_TEST(!structure.initial_unknowns[1].dependencies.has_value());

    BOOST_REQUIRE_EQUAL(structure.event_indicators.size(), 1U);
    BOOST_TEST(structure.event_indicators[0].variable.reference == 5U);
    BOOST_REQUIRE(structure.event_indicators[0].dependencies);
    BOOST_TEST(structure.event_indicators[0].dependencies->empty());

    const auto invalid = directory.path() / "structure3-invalid.fmu";
    cosim::test::write_archive(invalid, makeXml("structure-3-invalid", R"(dependencies="999")"));
    BOOST_CHECK_THROW(importer->import(invalid), cosim::error);
}

BOOST_AUTO_TEST_CASE(fmi3_invalid_output_dependency_metadata_is_rejected)
{
    const auto directory = cosim::utility::temp_dir();
    const auto importer = fmi::importer::create();
    const std::string prefix = R"(
<fmiModelDescription fmiVersion="3.0" modelName="InvalidDependencies" instantiationToken="invalid-dependencies">
  <CoSimulation modelIdentifier="InvalidDependencies"/>
  <ModelVariables>
    <Float64 name="inputA" valueReference="1" causality="input" start="0"/>
    <Float64 name="inputB" valueReference="2" causality="input" start="0"/>
    <Float64 name="output" valueReference="3" causality="output"/>
  </ModelVariables>
  <ModelStructure><Output valueReference="3" )";
    const std::string suffix = R"(/><InitialUnknown valueReference="3"/></ModelStructure>
</fmiModelDescription>)";
    const std::array<std::string, 3> malformed{
        R"(dependencies="999")",
        R"(dependencies="1 2" dependenciesKind="dependent")",
        R"(dependencies="1" dependenciesKind="invalid")"};
    for (std::size_t i = 0; i < malformed.size(); ++i) {
        const auto archive = directory.path() / ("invalid" + std::to_string(i) + ".fmu");
        cosim::test::write_archive(archive, prefix + malformed[i] + suffix);
        BOOST_CHECK_THROW(importer->import(archive), cosim::error);
    }
}

namespace
{
std::string substitute(std::string text, const std::string& marker, const std::string& value)
{
    for (auto position = text.find(marker); position != std::string::npos;
         position = text.find(marker, position + value.size())) {
        text.replace(position, marker.size(), value);
    }
    return text;
}

std::string with_crlf(const std::string& text)
{
    std::string result;
    for (const char c : text) {
        if (c == '\n') result += '\r';
        result += c;
    }
    return result;
}

// One output with two dependencies and one initial unknown with one. The
// comment and the CRLF line endings must survive normalization.
std::string dependency_fixture(
    bool fmi3,
    const std::string& token,
    const std::string& outputDependencies,
    const std::string& initialDependencies)
{
    const std::string fmi3xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<fmiModelDescription fmiVersion="3.0" modelName="Plus3" instantiationToken="@TOKEN@">
  <CoSimulation modelIdentifier="Plus3"/>
  <ModelVariables>
    <Float64 name="inputA" valueReference="21" causality="input" start="0"/>
    <Int32 name="inputB" valueReference="5" causality="input" start="0"/>
    <Float64 name="dependent" valueReference="11" causality="output"/>
  </ModelVariables>
  <ModelStructure>
    <!-- dependencies="+9" inside a comment is not an attribute -->
    <Output valueReference="11" @OUTPUT@ dependenciesKind="fixed discrete"/>
    <InitialUnknown valueReference="11" @INITIAL@/>
  </ModelStructure>
</fmiModelDescription>
)";
    const std::string fmi2xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<fmiModelDescription fmiVersion="2.0" modelName="Plus2" guid="@TOKEN@" numberOfEventIndicators="0">
  <CoSimulation modelIdentifier="Plus2"/>
  <ModelVariables>
    <ScalarVariable name="inputA" valueReference="21" causality="input" variability="continuous"><Real start="0"/></ScalarVariable>
    <ScalarVariable name="inputB" valueReference="5" causality="input" variability="discrete"><Integer start="0"/></ScalarVariable>
    <ScalarVariable name="dependent" valueReference="11" causality="output" variability="continuous" initial="calculated"><Real/></ScalarVariable>
  </ModelVariables>
  <ModelStructure>
    <!-- dependencies="+9" inside a comment is not an attribute -->
    <Outputs><Unknown index="+3" @OUTPUT@ dependenciesKind="fixed discrete"/></Outputs>
    <InitialUnknowns><Unknown index="3" @INITIAL@/></InitialUnknowns>
  </ModelStructure>
</fmiModelDescription>
)";
    auto text = substitute(fmi3 ? fmi3xml : fmi2xml, "@TOKEN@", token);
    text = substitute(text, "@OUTPUT@", outputDependencies);
    text = substitute(text, "@INITIAL@", initialDependencies);
    return with_crlf(text);
}

std::string read_text_file(const cosim::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    BOOST_REQUIRE(input);
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

cosim::filesystem::path fmu_directory(const std::shared_ptr<fmi::fmu>& fmu)
{
    if (const auto v3 = std::dynamic_pointer_cast<fmi::v3::fmu>(fmu)) {
        return v3->directory();
    }
    return std::dynamic_pointer_cast<fmi::v2::fmu>(fmu)->directory();
}

void check_normalized_dependencies(const cosim::model_description& description)
{
    const auto* dependent = find_model_variable(description, "dependent");
    BOOST_REQUIRE(dependent);
    BOOST_REQUIRE(dependent->output_dependencies);
    BOOST_REQUIRE_EQUAL(dependent->output_dependencies->size(), 2U);
    const auto& dependencies = *dependent->output_dependencies;
    BOOST_TEST(dependencies[0].variable.reference == 5U);
    BOOST_CHECK(dependencies[0].variable.type == variable_type::integer);
    BOOST_CHECK(dependencies[0].kind == dependency_kind::fixed);
    BOOST_TEST(dependencies[1].variable.reference == 21U);
    BOOST_CHECK(dependencies[1].variable.type == variable_type::real);
    BOOST_CHECK(dependencies[1].kind == dependency_kind::discrete);
}
} // namespace

BOOST_AUTO_TEST_CASE(leading_plus_in_dependency_lists_is_normalized_for_archives)
{
    const auto directory = cosim::utility::temp_dir();
    for (const bool fmi3 : {false, true}) {
        const std::string token = fmi3 ? "plus-archive-3" : "{plus-archive-2}";
        const auto outputPlus = fmi3 ? R"(dependencies="+5  +21")" : R"(dependencies='+2 +1')";
        const auto outputPlain = fmi3 ? R"(dependencies="5  21")" : R"(dependencies='2 1')";
        const auto initialPlus = fmi3 ? R"(dependencies="+5")" : R"(dependencies="+1")";
        const auto initialPlain = fmi3 ? R"(dependencies="5")" : R"(dependencies="1")";
        const auto archive = directory.path() / (fmi3 ? "plus3.fmu" : "plus2.fmu");
        cosim::test::write_archive(archive, dependency_fixture(fmi3, token, outputPlus, initialPlus));
        const auto expected = dependency_fixture(fmi3, token, outputPlain, initialPlain);

        // A shared persistent cache makes the second import reuse the entry.
        const auto cache = std::make_shared<persistent_file_cache>(
            directory.path() / (fmi3 ? "cache3" : "cache2"));
        for (int round = 0; round < 2; ++round) {
            const auto importer = fmi::importer::create(cache);
            const auto fmu = importer->import(archive);
            BOOST_REQUIRE(fmu);
            check_normalized_dependencies(*fmu->model_description());
            BOOST_TEST(
                read_text_file(fmu_directory(fmu) / "modelDescription.xml") == expected);
        }
    }
}

BOOST_AUTO_TEST_CASE(leading_plus_in_dependency_lists_is_normalized_for_unpacked_fmus)
{
    const auto directory = cosim::utility::temp_dir();
    for (const bool fmi3 : {false, true}) {
        const std::string token = fmi3 ? "plus-unpacked-3" : "{plus-unpacked-2}";
        const auto outputPlus = fmi3 ? R"(dependencies="+5  +21")" : R"(dependencies='+2 +1')";
        const auto outputPlain = fmi3 ? R"(dependencies="5  21")" : R"(dependencies='2 1')";
        const auto initialPlus = fmi3 ? R"(dependencies="+5")" : R"(dependencies="+1")";
        const auto initialPlain = fmi3 ? R"(dependencies="5")" : R"(dependencies="1")";
        const auto original = dependency_fixture(fmi3, token, outputPlus, initialPlus);
        const auto expected = dependency_fixture(fmi3, token, outputPlain, initialPlain);

        const auto unpacked = directory.path() / (fmi3 ? "unpacked3" : "unpacked2");
        cosim::filesystem::create_directories(unpacked / "resources");
        {
            std::ofstream output(unpacked / "modelDescription.xml", std::ios::binary);
            output << original;
            output.close();
            BOOST_REQUIRE(output);
            std::ofstream resource(unpacked / "resources" / "data.txt", std::ios::binary);
            resource << "payload";
            resource.close();
            BOOST_REQUIRE(resource);
        }

        cosim::filesystem::path copy;
        {
            const auto importer = fmi::importer::create();
            const auto fmu = importer->import_unpacked(unpacked);
            BOOST_REQUIRE(fmu);
            check_normalized_dependencies(*fmu->model_description());

            // The caller's directory is not modified; FMI Library reads a copy.
            BOOST_TEST(read_text_file(unpacked / "modelDescription.xml") == original);
            copy = fmu_directory(fmu);
            BOOST_TEST(copy != unpacked);
            BOOST_TEST(read_text_file(copy / "modelDescription.xml") == expected);
            BOOST_TEST(read_text_file(copy / "resources" / "data.txt") == "payload");
        }
        BOOST_TEST(!cosim::filesystem::exists(copy));
    }
}

BOOST_AUTO_TEST_CASE(unpacked_fmus_without_leading_plus_are_used_in_place)
{
    const auto directory = cosim::utility::temp_dir();
    for (const bool fmi3 : {false, true}) {
        const std::string token = fmi3 ? "plain-unpacked-3" : "{plain-unpacked-2}";
        const auto unpacked = directory.path() / (fmi3 ? "plain3" : "plain2");
        cosim::filesystem::create_directories(unpacked);
        {
            std::ofstream output(unpacked / "modelDescription.xml", std::ios::binary);
            output << dependency_fixture(
                fmi3, token,
                fmi3 ? R"(dependencies="5 21")" : R"(dependencies="2 1")",
                fmi3 ? R"(dependencies="5")" : R"(dependencies="1")");
            output.close();
            BOOST_REQUIRE(output);
        }
        const auto importer = fmi::importer::create();
        const auto fmu = importer->import_unpacked(unpacked);
        BOOST_REQUIRE(fmu);
        check_normalized_dependencies(*fmu->model_description());
        BOOST_TEST(fmu_directory(fmu) == unpacked);
    }
}

BOOST_AUTO_TEST_CASE(unnormalizable_dependency_signs_are_rejected_before_fmilib_parsing)
{
    const auto directory = cosim::utility::temp_dir();
    const auto importer = fmi::importer::create();
    struct fixture
    {
        std::string attributes;
        std::string section;
        bool hangsFmiLibrary;
    };
    const std::array<fixture, 8> fixtures{{
        {"fmiVersion=\"2.0\" guid=\"{guard-2-entity}\"",
            "<Outputs><Unknown index=\"1\" dependencies=\"&#43;1\"/></Outputs>", true},
        {"fmiVersion=\"3.0\" instantiationToken=\"guard-3-entity\"",
            "<Output valueReference=\"1\" dependencies=\"&#43;1\"/>", true},
        {"fmiVersion=\"2.0\" guid=\"{guard-2-alone}\"",
            "<Outputs><Unknown index=\"1\" dependencies=\"1 +\"/></Outputs>", false},
        {"fmiVersion=\"3.0\" instantiationToken=\"guard-3-alone\"",
            "<Output valueReference=\"1\" dependencies=\"1 +\"/>", false},
        {"fmiVersion=\"2.0\" guid=\"{guard-2-inner}\"",
            "<InitialUnknowns><Unknown index=\"1\" dependencies=\"1+2\"/></InitialUnknowns>", false},
        {"fmiVersion=\"3.0\" instantiationToken=\"guard-3-inner\"",
            "<InitialUnknown valueReference=\"1\" dependencies=\"1+2\"/>", false},
        {"fmiVersion=\"2.0\" guid=\"{guard-2-double}\"",
            "<Outputs><Unknown index=\"1\" dependencies=\"+-1\"/></Outputs>", false},
        {"fmiVersion=\"3.0\" instantiationToken=\"guard-3-double\"",
            "<Output valueReference=\"1\" dependencies=\"++1\"/>", false},
    }};
    for (std::size_t i = 0; i < fixtures.size(); ++i) {
        const auto xml = "<fmiModelDescription " + fixtures[i].attributes +
            "><ModelStructure>" + fixtures[i].section +
            "</ModelStructure></fmiModelDescription>";
        const auto archive = directory.path() / ("guard-" + std::to_string(i) + ".fmu");
        cosim::test::write_archive(archive, xml);
        const auto expectedCode = make_error_code(
            fixtures[i].hangsFmiLibrary ? errc::unsupported_feature : errc::bad_file);
        const auto checkError = [&](const cosim::error& e) {
            return e.code() == expectedCode;
        };
        BOOST_CHECK_EXCEPTION(importer->import(archive), cosim::error, checkError);

        const auto unpacked = directory.path() / ("unpacked-" + std::to_string(i));
        cosim::filesystem::create_directory(unpacked);
        std::ofstream output(unpacked / "modelDescription.xml");
        output << xml;
        output.close();
        BOOST_REQUIRE(output);
        BOOST_CHECK_EXCEPTION(
            importer->import_unpacked(unpacked), cosim::error, checkError);
    }
}

BOOST_AUTO_TEST_CASE(fmi3_resource_directory_is_a_native_path)
{
    const auto path = reference_fmu_directory() / "Resource.fmu";
    require_fixture(path);

    utility::temp_dir directory;
    const auto unpacked = directory.path() / "unpacked resource fmu";
    cosim::filesystem::create_directory(unpacked);
    utility::zip::archive(path).extract_all(unpacked);

    for (const bool importUnpacked : {false, true}) {
        const auto importer = fmi::importer::create();
        const auto model = std::static_pointer_cast<fmi::v3::fmu>(
            importUnpacked ? importer->import_unpacked(unpacked) : importer->import(path));
        const auto references = variable_references(*model->model_description());
        const auto instance = model->instantiate_v3_slave("resource-path");
        instance->setup(time_point{}, to_time_point(1.0), std::nullopt);
        instance->start_simulation();
        const auto reference = references.at("y");
        int value = 0;
        instance->get_integer_variables(
            gsl::make_span(&reference, 1), gsl::make_span(&value, 1));
        BOOST_TEST(value == static_cast<int>('a'));
        instance->end_simulation();
    }
}

BOOST_AUTO_TEST_CASE(fmi3_scheduled_execution_only_is_rejected)
{
    const auto modelDirectory = cosim::utility::temp_dir();
    std::ofstream modelDescription(modelDirectory.path() / "modelDescription.xml");
    modelDescription
        << R"(<?xml version="1.0" encoding="UTF-8"?>
<fmiModelDescription fmiVersion="3.0" modelName="ScheduledExecutionOnly"
    instantiationToken="scheduled-execution-only">
  <ScheduledExecution modelIdentifier="ScheduledExecutionOnly"/>
  <ModelVariables>
    <Float64 name="output" valueReference="0" causality="output"/>
  </ModelVariables>
  <ModelStructure>
    <Output valueReference="0"/>
    <InitialUnknown valueReference="0"/>
  </ModelStructure>
</fmiModelDescription>
)";
    modelDescription.close();

    auto importer = fmi::importer::create();
    BOOST_CHECK_EXCEPTION(
        importer->import_unpacked(modelDirectory.path()),
        cosim::error,
        error_is(errc::unsupported_feature));
}

BOOST_AUTO_TEST_CASE(fmi3_model_exchange_is_rejected)
{
    const auto modelDirectory = cosim::utility::temp_dir();
    std::ofstream modelDescription(modelDirectory.path() / "modelDescription.xml");
    modelDescription
        << R"(<?xml version="1.0" encoding="UTF-8"?>
<fmiModelDescription fmiVersion="3.0" modelName="ModelExchangeOnly"
    instantiationToken="model-exchange-only">
  <ModelExchange modelIdentifier="ModelExchangeOnly"/>
  <ModelVariables>
    <Float64 name="output" valueReference="0" causality="output"/>
  </ModelVariables>
  <ModelStructure>
    <InitialUnknown valueReference="0"/>
  </ModelStructure>
</fmiModelDescription>
)";
    modelDescription.close();

    auto importer = fmi::importer::create();
    BOOST_CHECK_EXCEPTION(
        importer->import_unpacked(modelDirectory.path()),
        cosim::error,
        error_is(errc::unsupported_feature));
}


BOOST_AUTO_TEST_CASE(fmi3_enumeration_malformed_metadata)
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
    fmiVersion="3.0"
    modelName="BadEnumeration"
    instantiationToken="bad-enumeration">
  <CoSimulation modelIdentifier="BadEnumeration"/>
  <TypeDefinitions>)"
                << typeDefinition
                << R"(</TypeDefinitions>
  <ModelVariables>)"
                << variableDefinition
                << R"(</ModelVariables>
  <ModelStructure/>
</fmiModelDescription>
)";
            modelDescription.close();

            auto importer = fmi::importer::create();
            BOOST_CHECK_EXCEPTION(
                importer->import_unpacked(modelDirectory.path()),
                cosim::error,
                error_is(errc::bad_file));
        };
    const std::string validType = R"(
    <EnumerationType name="Option">
      <Item name="One" value="1"/>
      <Item name="Two" value="2"/>
    </EnumerationType>
)";
    const std::string validVariable = R"(
    <Enumeration name="input" valueReference="1"
        declaredType="Option" causality="input" variability="discrete"
        start="1"/>
)";

    importMustFail(
        validType,
        R"(
    <Enumeration name="input" valueReference="1"
        declaredType="Missing" causality="input" variability="discrete"
        start="1"/>
)");
    importMustFail(
        R"(
    <EnumerationType name="Option">
      <Item name="One" value="1"/>
      <Item name="One" value="2"/>
    </EnumerationType>
)",
        validVariable);
    importMustFail(
        R"(
    <EnumerationType name="Option">
      <Item name="One" value="1"/>
      <Item name="Two" value="1"/>
    </EnumerationType>
)",
        validVariable);
    importMustFail(
        validType,
        R"(
    <Enumeration name="input" valueReference="1"
        declaredType="Option" causality="input" variability="discrete"
        start="3"/>
)");
    importMustFail(
        validType,
        R"(
    <Enumeration name="input" valueReference="1"
        declaredType="Option" causality="input" variability="discrete"
        min="2" max="1"/>
)");
}


BOOST_AUTO_TEST_CASE(fmi3_array_metadata_and_legacy_access_validation)
{
    const auto fmuPath =
        array_fixture_directory() / "FixedArrayStateSpace.fmu";
    require_fixture(fmuPath);

    auto importer = fmi::importer::create();
    auto fmu = std::static_pointer_cast<fmi::v3::fmu>(
        importer->import(fmuPath));
    const auto description = fmu->model_description();

    const auto* m = find_model_variable(*description, "m");
    BOOST_REQUIRE(m != nullptr);
    BOOST_TEST(m->type == variable_type::uint64);
    BOOST_TEST(m->causality == variable_causality::structural_parameter);
    BOOST_REQUIRE(m->start.has_value());
    BOOST_TEST(std::get<std::uint64_t>(*m->start) == 3U);
    BOOST_REQUIRE(m->min.has_value());
    BOOST_REQUIRE(m->max.has_value());
    BOOST_TEST(std::get<std::uint64_t>(*m->min) == 0U);
    BOOST_TEST(std::get<std::uint64_t>(*m->max) == 5U);

    const auto* matrixA = find_model_variable(*description, "A");
    BOOST_REQUIRE(matrixA != nullptr);
    BOOST_REQUIRE_EQUAL(matrixA->dimensions.size(), 2U);
    BOOST_TEST(
        std::get<variable_dimension>(matrixA->dimensions[0])
            .structural_parameter == 2U);
    BOOST_TEST(
        std::get<variable_dimension>(matrixA->dimensions[1])
            .structural_parameter == 2U);
    BOOST_REQUIRE(matrixA->array_start.has_value());
    const std::uint64_t expectedMatrixShape[] = {3U, 3U};
    BOOST_CHECK_EQUAL_COLLECTIONS(
        matrixA->array_start->dimensions().begin(),
        matrixA->array_start->dimensions().end(),
        std::begin(expectedMatrixShape),
        std::end(expectedMatrixShape));
    const auto& matrixValues =
        std::get<real_values>(matrixA->array_start->values()).values;
    const double expectedMatrixValues[] = {
        1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};
    BOOST_CHECK_EQUAL_COLLECTIONS(
        matrixValues.begin(),
        matrixValues.end(),
        std::begin(expectedMatrixValues),
        std::end(expectedMatrixValues));

    const auto* fixed = find_model_variable(*description, "fixed");
    BOOST_REQUIRE(fixed != nullptr);
    BOOST_REQUIRE_EQUAL(fixed->dimensions.size(), 2U);
    BOOST_TEST(
        std::get<fixed_dimension>(fixed->dimensions[0]).size == 2U);
    BOOST_TEST(
        std::get<fixed_dimension>(fixed->dimensions[1]).size == 2U);
    BOOST_REQUIRE(fixed->array_start.has_value());
    const double expectedFixedValues[] = {1.0, 0.0, 0.0, 1.0};
    const auto& fixedStartValues =
        std::get<real_values>(fixed->array_start->values()).values;
    BOOST_CHECK_EQUAL_COLLECTIONS(
        fixedStartValues.begin(),
        fixedStartValues.end(),
        std::begin(expectedFixedValues),
        std::end(expectedFixedValues));

    const auto* input = find_model_variable(*description, "u");
    BOOST_REQUIRE(input != nullptr);
    BOOST_REQUIRE_EQUAL(input->dimensions.size(), 1U);
    BOOST_TEST(
        std::get<variable_dimension>(input->dimensions[0])
            .structural_parameter == 1U);
    BOOST_REQUIRE(input->array_start.has_value());
    const auto& inputValues =
        std::get<real_values>(input->array_start->values()).values;
    const double expectedInputValues[] = {1.0, 2.0, 3.0};
    BOOST_CHECK_EQUAL_COLLECTIONS(
        inputValues.begin(),
        inputValues.end(),
        std::begin(expectedInputValues),
        std::end(expectedInputValues));

    auto instance = fmu->instantiate_v3_slave("fmi3-configuration");
    const value_reference matrixReference = 7;
    const std::uint64_t initialMatrixShape[] = {3U, 3U};
    auto matrixShape =
        instance->resolved_shape(variable_type::real, matrixReference);
    BOOST_CHECK_EQUAL_COLLECTIONS(
        matrixShape.begin(),
        matrixShape.end(),
        std::begin(initialMatrixShape),
        std::end(initialMatrixShape));
    const typed_value_reference genericMatrix = {
        variable_type::real, matrixReference};
    const auto genericMatrixValues =
        instance->get_variable_values(gsl::make_span(&genericMatrix, 1));
    BOOST_REQUIRE_EQUAL(genericMatrixValues.size(), 1U);
    BOOST_TEST(genericMatrixValues[0].dimensions() == matrixShape);
    BOOST_TEST(genericMatrixValues[0].size() == 9U);
    const variable_value replacementMatrix(
        real_values{{2.0, 0.0, 0.0, 0.0, 2.0, 0.0, 0.0, 0.0, 2.0}},
        {3U, 3U});
    instance->set_variable_values(
        gsl::make_span(&genericMatrix, 1),
        gsl::make_span(&replacementMatrix, 1));
    const typed_value_reference fixedReference{
        variable_type::real, fixed->reference};
    const variable_value fixedValue(
        real_values{{2.0, 3.0, 5.0, 7.0}}, {2U, 2U});
    instance->set_variable_values(
        gsl::make_span(&fixedReference, 1),
        gsl::make_span(&fixedValue, 1));
    const auto fixedValues = instance->get_variable_values(
        gsl::make_span(&fixedReference, 1));
    BOOST_REQUIRE_EQUAL(fixedValues.size(), 1U);
    BOOST_TEST(fixedValues[0].dimensions() == fixedValue.dimensions());
    BOOST_TEST(
        std::get<real_values>(fixedValues[0].values()).values
        == std::get<real_values>(fixedValue.values()).values);

    const value_reference uint64Reference = 1;
    int integerValue = 1;
    BOOST_CHECK_EXCEPTION(
        instance->set_integer_variables(
            gsl::make_span(&uint64Reference, 1),
            gsl::make_span(&integerValue, 1)),
        cosim::error,
        error_is(errc::model_error));

    const value_reference arrayReference = 4;
    double realValue = 1.0;
    BOOST_CHECK_THROW(
        instance->set_real_variables(
            gsl::make_span(&arrayReference, 1),
            gsl::make_span(&realValue, 1)),
        std::invalid_argument);

    instance->enter_configuration_mode();
    const value_reference structuralReferences[] = {1, 2, 3};
    const std::uint64_t configuredSizes[] = {2, 0, 2};
    try {
        instance->set_uint64_variables(
            structuralReferences, configuredSizes);
    } catch (const cosim::error& error) {
        if (std::string(error.what()).find("Expected nValues >=") != std::string::npos) {
            BOOST_TEST_MESSAGE(
                "Skipping StateSpace reconfiguration checks because this "
                "Reference FMU build has a known SetUInt64 count defect");
            return;
        }
        throw;
    }
    const std::uint64_t configuredMatrixShape[] = {2U, 2U};
    matrixShape =
        instance->resolved_shape(variable_type::real, matrixReference);
    BOOST_CHECK_EQUAL_COLLECTIONS(
        matrixShape.begin(),
        matrixShape.end(),
        std::begin(configuredMatrixShape),
        std::end(configuredMatrixShape));
    instance->exit_configuration_mode();

    const auto configuredState = instance->save_state();
    instance->enter_configuration_mode();
    const value_reference resizedReferences[] = {1, 3};
    const std::uint64_t resizedValues[] = {3, 3};
    instance->set_uint64_variables(resizedReferences, resizedValues);
    instance->exit_configuration_mode();
    const std::uint64_t resizedMatrixShape[] = {3U, 3U};
    matrixShape =
        instance->resolved_shape(variable_type::real, matrixReference);
    BOOST_CHECK_EQUAL_COLLECTIONS(
        matrixShape.begin(),
        matrixShape.end(),
        std::begin(resizedMatrixShape),
        std::end(resizedMatrixShape));

    instance->restore_state(configuredState);
    instance->release_state(configuredState);
    matrixShape =
        instance->resolved_shape(variable_type::real, matrixReference);
    BOOST_CHECK_EQUAL_COLLECTIONS(
        matrixShape.begin(),
        matrixShape.end(),
        std::begin(configuredMatrixShape),
        std::end(configuredMatrixShape));

    const double configuredMatrixValues[] = {1.0, 0.0, 0.0, 1.0};
    instance->set_real_variables(
        gsl::make_span(&matrixReference, 1),
        configuredMatrixValues);
    instance->setup(to_time_point(0.0), to_time_point(1.0), std::nullopt);
}


BOOST_AUTO_TEST_CASE(fmi3_declared_type_unit_alias_and_numeric_metadata)
{
    const auto fmuPath = reference_fmu_directory() / "BouncingBall.fmu";
    require_fixture(fmuPath);

    auto importer = fmi::importer::create();
    const auto description = importer->import(fmuPath)->model_description();

    const auto* metre = find_unit_definition(*description, "m");
    BOOST_REQUIRE(metre != nullptr);
    BOOST_TEST(metre->has_si_base_unit);
    BOOST_TEST(metre->metre == 1);
    BOOST_REQUIRE_EQUAL(metre->display_units.size(), 1U);
    BOOST_TEST(metre->display_units[0].name == "ft");
    BOOST_TEST(
        metre->display_units[0].factor == 3.280839895,
        boost::test_tools::tolerance(1e-12));

    const auto* position =
        find_type_definition(*description, "Position");
    BOOST_REQUIRE(position != nullptr);
    BOOST_REQUIRE(position->quantity.has_value());
    BOOST_REQUIRE(position->unit.has_value());
    BOOST_TEST(*position->quantity == "Position");
    BOOST_TEST(*position->unit == "m");

    const auto* height = find_model_variable(*description, "h");
    BOOST_REQUIRE(height != nullptr);
    BOOST_REQUIRE(height->declared_type.has_value());
    BOOST_TEST(*height->declared_type == "Position");
    BOOST_TEST(height->initial.has_value());
    BOOST_TEST(*height->initial == variable_initial::exact);
    BOOST_REQUIRE(height->start.has_value());
    BOOST_TEST(std::get<double>(*height->start) == 1.0);
    BOOST_REQUIRE(height->reinit.has_value());
    BOOST_TEST(*height->reinit);
    BOOST_TEST(height->intermediate_update);
    BOOST_REQUIRE_EQUAL(height->aliases.size(), 1U);
    BOOST_TEST(height->aliases[0].name == "h_ft");
    BOOST_REQUIRE(height->aliases[0].description.has_value());
    BOOST_REQUIRE(height->aliases[0].display_unit.has_value());
    BOOST_TEST(*height->aliases[0].description == "Position in feet");
    BOOST_TEST(*height->aliases[0].display_unit == "ft");
    const auto heightAlias = cosim::find_variable(*description, "h_ft");
    BOOST_REQUIRE(heightAlias);
    BOOST_TEST(heightAlias->reference == height->reference);
    BOOST_TEST(heightAlias->description == "Position in feet");
    BOOST_REQUIRE(heightAlias->display_unit);
    BOOST_TEST(*heightAlias->display_unit == "ft");

    const auto* heightDerivative =
        find_model_variable(*description, "der(h)");
    BOOST_REQUIRE(heightDerivative != nullptr);
    BOOST_REQUIRE(heightDerivative->derivative.has_value());
    BOOST_TEST(*heightDerivative->derivative == 1U);

    const auto* restitution = find_model_variable(*description, "e");
    BOOST_REQUIRE(restitution != nullptr);
    BOOST_REQUIRE(restitution->min.has_value());
    BOOST_REQUIRE(restitution->max.has_value());
    BOOST_TEST(std::get<double>(*restitution->min) == 0.5);
    BOOST_TEST(std::get<double>(*restitution->max) == 1.0);
}

BOOST_AUTO_TEST_CASE(fmi3_si_base_unit_presence)
{
    const auto fmuPath = reference_fmu_directory() / "BouncingBall.fmu";
    require_fixture(fmuPath);
    utility::temp_dir directory;
    utility::zip::archive(fmuPath).extract_all(directory.path());
    const auto path = directory.path() / "modelDescription.xml";
    std::ifstream input(path);
    BOOST_REQUIRE(input);
    std::string xml(
        (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    input.close();
    const auto position = xml.find("</UnitDefinitions>");
    BOOST_REQUIRE(position != std::string::npos);
    xml.insert(position,
        "<Unit name=\"dimensionless\"><BaseUnit/></Unit>"
        "<Unit name=\"opaque\"/>");
    std::ofstream output(path, std::ios::trunc);
    BOOST_REQUIRE(output);
    output << xml;
    output.close();
    auto importer = fmi::importer::create();
    const auto description =
        importer->import_unpacked(directory.path())->model_description();
    const auto* dimensionless = find_unit_definition(*description, "dimensionless");
    const auto* opaque = find_unit_definition(*description, "opaque");
    BOOST_REQUIRE(dimensionless != nullptr);
    BOOST_REQUIRE(opaque != nullptr);
    BOOST_TEST(dimensionless->has_si_base_unit);
    BOOST_TEST(!opaque->has_si_base_unit);
}


BOOST_AUTO_TEST_CASE(fmi3_nominal_metadata)
{
    const auto fmuPath = reference_fmu_directory() / "Roberts.fmu";
    require_fixture(fmuPath);

    auto importer = fmi::importer::create();
    const auto description = importer->import(fmuPath)->model_description();
    const auto* y1 = find_model_variable(*description, "y1");
    BOOST_REQUIRE(y1 != nullptr);
    BOOST_REQUIRE(y1->nominal.has_value());
    BOOST_TEST(
        std::get<double>(*y1->nominal) == 1e-4,
        boost::test_tools::tolerance(1e-12));

    const auto* y3 = find_model_variable(*description, "y3");
    BOOST_REQUIRE(y3 != nullptr);
    BOOST_REQUIRE(y3->initial.has_value());
    BOOST_TEST(*y3->initial == variable_initial::approximate);
}
