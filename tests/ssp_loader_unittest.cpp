#define BOOST_TEST_MODULE ssp_loader.hpp unittests
#include <cosim/algorithm/fixed_step_algorithm.hpp>
#include <cosim/exception.hpp>
#include <cosim/fs_portability.hpp>
#include <cosim/log/simple.hpp>
#include <cosim/observer/last_value_observer.hpp>
#include <cosim/osp_config_parser.hpp>
#include <cosim/ssp/ssp_loader.hpp>
#include <cosim/utility/filesystem.hpp>

#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>


namespace
{

// Defines the tolerance for the comparison in percentage units.
// See https://www.boost.org/doc/libs/1_65_0/libs/test/doc/html/boost_test/utf_reference/testing_tool_ref/assertion_boost_level_close.html
constexpr double tolerance = 0.0001;

void common_demo_case_tests(const cosim::ssp_configuration& config)
{
    auto execution = cosim::execution(config.start_time, config.algorithm);
    const auto entityMaps = cosim::inject_system_structure(
        execution,
        config.system_structure,
        config.parameter_sets.at(""));

    BOOST_REQUIRE(entityMaps.simulators.size() == 2);
    BOOST_REQUIRE(entityMaps.simulators.count("CraneController") == 1);
    const auto knuckleBoomCrane = entityMaps.simulators.at("KnuckleBoomCrane");

    auto obs = std::make_shared<cosim::last_value_observer>();
    execution.add_observer(obs);
    auto result = execution.simulate_until(cosim::to_time_point(1e-3));
    BOOST_REQUIRE(result);

    double realValue = -1.0;
    const auto reference =
        config.system_structure.get_variable_description({"KnuckleBoomCrane", "Spring_Joint.k"}).reference;
    obs->get_real(knuckleBoomCrane, gsl::make_span(&reference, 1), gsl::make_span(&realValue, 1));

    double magicNumberFromSsdFile = 0.005;
    BOOST_CHECK_CLOSE(realValue, magicNumberFromSsdFile, tolerance);

    const auto reference2 =
        config.system_structure.get_variable_description({"KnuckleBoomCrane", "mt0_init"}).reference;
    obs->get_real(knuckleBoomCrane, gsl::make_span(&reference2, 1), gsl::make_span(&realValue, 1));

    magicNumberFromSsdFile = 69.0;
    BOOST_CHECK_CLOSE(realValue, magicNumberFromSsdFile, tolerance);
}

} // namespace

BOOST_AUTO_TEST_CASE(unsupported_types_report_input_errors)
{
    cosim::utility::temp_dir directory;
    const auto path = directory.path() / "invalid.ssd";
    cosim::ssp_loader loader;
    const auto check = [&](const std::string& component, const std::string& expected) {
        std::ofstream file(path);
        file << "<ssd:SystemStructureDescription name=\"test\" version=\"1.0\">"
                "<ssd:System name=\"system\"><ssd:Elements>"
                "<ssd:Component name=\"unit\" source=\"unit.fmu\">"
             << component
             << "</ssd:Component></ssd:Elements></ssd:System>"
                "</ssd:SystemStructureDescription>";
        file.close();
        BOOST_REQUIRE(!file.fail());
        BOOST_CHECK_EXCEPTION(
            loader.load(path),
            std::invalid_argument,
            [&expected](const std::invalid_argument& error) {
                return std::string(error.what()).find(expected) != std::string::npos;
            });
    };
    for (const auto* type : {"Enumeration", "Binary"}) {
        check(
            std::string("<ssd:Connectors><ssd:Connector name=\"out\" kind=\"output\"><ssc:") +
                type + "/></ssd:Connector></ssd:Connectors>",
            type);
    }
    for (const auto* parameterType : {"<ssv:Enumeration value=\"1\"/>", "<ssv:Binary value=\"00\"/>", ""}) {
        check(
            std::string("<ssd:ParameterBindings><ssd:ParameterBinding><ssd:ParameterValues>"
                        "<ssv:ParameterSet name=\"set\"><ssv:Parameters>"
                        "<ssv:Parameter name=\"mode\">") +
                parameterType +
                "</ssv:Parameter></ssv:Parameters></ssv:ParameterSet></ssd:ParameterValues>"
                "</ssd:ParameterBinding></ssd:ParameterBindings>",
            "mode");
    }
}

BOOST_AUTO_TEST_CASE(basic_test)
{
    cosim::log::setup_simple_console_logging();
    cosim::log::set_global_output_level(cosim::log::info);

    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_REQUIRE(testDataDir != nullptr);
    cosim::filesystem::path sspFile = cosim::filesystem::path(testDataDir) / "ssp" / "demo";

    cosim::ssp_loader loader;
    const auto config = loader.load(sspFile);

    const auto fixed = std::dynamic_pointer_cast<cosim::fixed_step_algorithm>(config.algorithm);
    BOOST_REQUIRE(fixed);
    BOOST_CHECK_THROW(fixed->set_max_event_iterations(0), std::invalid_argument);
    fixed->set_max_event_iterations(5);
    common_demo_case_tests(config);
    BOOST_CHECK_THROW(fixed->set_max_event_iterations(10), cosim::error);
}

BOOST_AUTO_TEST_CASE(no_algorithm_test)
{
    cosim::log::setup_simple_console_logging();
    cosim::log::set_global_output_level(cosim::log::info);

    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_REQUIRE(testDataDir != nullptr);
    cosim::filesystem::path sspFile = cosim::filesystem::path(testDataDir) / "ssp" / "demo" / "no_algorithm_element";

    cosim::ssp_loader loader;
    auto config = loader.load(sspFile);
    config.algorithm = std::make_unique<cosim::fixed_step_algorithm>(cosim::to_duration(1e-4));

    double startTimeDefinedInSsp = 5.0;
    BOOST_CHECK_CLOSE(cosim::to_double_time_point(config.start_time), startTimeDefinedInSsp, tolerance);

    common_demo_case_tests(config);
}

BOOST_AUTO_TEST_CASE(ssp_archive)
{
    cosim::log::setup_simple_console_logging();
    cosim::log::set_global_output_level(cosim::log::info);

    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_TEST_REQUIRE(testDataDir != nullptr);
    const auto sspFile = cosim::filesystem::path(testDataDir) / "ssp" / "demo" / "demo.ssp";

    cosim::ssp_loader loader;
    std::optional<cosim::time_point> endTime;
    const auto config = loader.load(sspFile, endTime);
    BOOST_REQUIRE(endTime);
    BOOST_CHECK_CLOSE(cosim::to_double_time_point(*endTime), 2e-4, tolerance);
    common_demo_case_tests(config);
}

BOOST_AUTO_TEST_CASE(ssp_archive_multiple_ssd)
{
    cosim::log::setup_simple_console_logging();
    cosim::log::set_global_output_level(cosim::log::info);

    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_TEST_REQUIRE(testDataDir != nullptr);
    const auto sspFile = cosim::filesystem::path(testDataDir) / "ssp" / "demo" / "demo.ssp";

    cosim::ssp_loader loader;
    loader.set_ssd_file_name("SystemStructure2");
    std::optional<cosim::time_point> endTime = cosim::to_time_point(3.0);
    const auto config = loader.load(sspFile, endTime);
    BOOST_CHECK(!endTime);
    const auto entities = config.system_structure.entities();
    BOOST_REQUIRE(std::distance(entities.begin(), entities.end()) == 1);
}

BOOST_AUTO_TEST_CASE(ssp_linear_transformation_test)
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_TEST_REQUIRE(testDataDir != nullptr);
    const auto sspDir = cosim::filesystem::path(testDataDir) / "ssp" / "linear_transformation";

    cosim::ssp_loader loader;
    const auto config = loader.load(sspDir);

    auto exec = cosim::execution(
        config.start_time,
        std::make_unique<cosim::fixed_step_algorithm>(cosim::to_duration(1e-3)));
    const auto entityMaps = cosim::inject_system_structure(
        exec,
        config.system_structure,
        config.parameter_sets.at(""));

    const auto observer = std::make_shared<cosim::last_value_observer>();
    exec.add_observer(observer);

    exec.step();

    double initialValue;
    const auto slave1 = entityMaps.simulators.at("identity1");
    const auto v1Ref =
        config.system_structure.get_variable_description({"identity1", "realOut"}).reference;
    observer->get_real(slave1, gsl::make_span(&v1Ref, 1), gsl::make_span(&initialValue, 1));
    BOOST_REQUIRE_CLOSE(initialValue, 2.0, tolerance);

    double transformedValue;
    const auto slave2 = entityMaps.simulators.at("identity2");
    const auto v2Ref =
        config.system_structure.get_variable_description({"identity2", "realIn"}).reference;
    observer->get_real(slave2, gsl::make_span(&v2Ref, 1), gsl::make_span(&transformedValue, 1));

    double offset = 50;
    double factor = 1.3;
    double target = factor * initialValue + offset;
    BOOST_REQUIRE_CLOSE(transformedValue, target, tolerance);
}

BOOST_AUTO_TEST_CASE(ssp_multiple_parameter_sets_test)
{
    const auto testDataDir = std::getenv("TEST_DATA_DIR");
    BOOST_TEST_REQUIRE(testDataDir != nullptr);
    const auto sspDir = cosim::filesystem::path(testDataDir) / "ssp" / "linear_transformation";

    cosim::ssp_loader loader;
    const auto config = loader.load(sspDir);

    auto exec = cosim::execution(
        config.start_time,
        std::make_unique<cosim::fixed_step_algorithm>(cosim::to_duration(1e-3)));
    const auto entityMaps = cosim::inject_system_structure(
        exec,
        config.system_structure,
        config.parameter_sets.at("initialValues2"));

    auto observer = std::make_shared<cosim::last_value_observer>();
    exec.add_observer(observer);

    exec.step();

    double initialValue;
    const auto slave1 = entityMaps.simulators.at("identity1");
    const auto v1Ref =
        config.system_structure.get_variable_description({"identity1", "realOut"}).reference;
    observer->get_real(slave1, gsl::make_span(&v1Ref, 1), gsl::make_span(&initialValue, 1));
    BOOST_REQUIRE_CLOSE(initialValue, 4.0, tolerance);
}

BOOST_AUTO_TEST_SUITE(xml_system_structure)

namespace
{
using namespace cosim;

filesystem::path fixture_path(const filesystem::path& relative)
{
    const auto* data = std::getenv("TEST_DATA_DIR");
    BOOST_REQUIRE(data != nullptr);
    return filesystem::path(data) / relative;
}

std::string read_xml(const filesystem::path& path)
{
    const std::ifstream in(path);
    BOOST_REQUIRE_MESSAGE(in.good(), "Cannot read " << path);
    return {std::istreambuf_iterator<char>(in.rdbuf()), {}};
}

void write_xml(const filesystem::path& path, const std::string& text)
{
    std::ofstream out(path);
    out << text;
    out.close();
    BOOST_REQUIRE_MESSAGE(out.good(), "Cannot write " << path);
}

std::string replace_xml(std::string text, std::string_view from, std::string_view to)
{
    const auto at = text.find(from);
    BOOST_REQUIRE(at != std::string::npos);
    text.replace(at, from.size(), to);
    return text;
}

void check_exact_values(const system_structure& structure, const variable_value_map& values)
{
    const std::pair<std::string_view, scalar_value> scalars[]{
        {"Float64_continuous", 1.25}, {"Float32_continuous", -2.5f},
        {"Int8", std::int8_t{-128}}, {"UInt8", std::uint8_t{255}},
        {"Int16", std::int16_t{-32768}}, {"UInt16", std::uint16_t{65535}},
        {"Int32", std::numeric_limits<int>::min()}, {"UInt32", std::numeric_limits<std::uint32_t>::max()},
        {"Int64", std::numeric_limits<std::int64_t>::min()}, {"UInt64", std::numeric_limits<std::uint64_t>::max()},
        {"Boolean", true}, {"String", std::string("  exact text  ")},
        {"Binary", binary_value{{0, 10, 254, 255}}}, {"Enumeration", enumeration_value{2}},
    };
    const std::pair<const char*, variable_value> arrays[]{
        {"String_array", variable_value(string_values{{"  two words  ", ""}}, {2})},
        {"Binary_array", variable_value(binary_values{{binary_value{{0, 255}}, binary_value{}}}, {2})},
        {"Enumeration_array", variable_value(enumeration_values{{{2}, {1}}}, {2})},
    };

    BOOST_REQUIRE_EQUAL(values.size(), std::size(scalars) + std::size(arrays));
    execution exe(time_point{}, std::make_shared<fixed_step_algorithm>(to_duration(0.1)));
    auto observer = std::make_shared<last_value_observer>();
    exe.add_observer(observer);
    const auto entities = inject_system_structure(exe, structure, values);
    BOOST_REQUIRE(exe.simulate_until(to_time_point(0.1)));
    const auto observed = [&](const full_variable_name& name) {
        const auto& variable = structure.get_variable_description(name);
        return observer->get_variable_value(entities.simulators.at(name.entity_name), variable.type, variable.reference);
    };
    for (const auto& [prefix, scalar] : scalars) {
        BOOST_TEST_CONTEXT(prefix) {
            const auto name = std::string(prefix);
            const auto expected = variable_value(scalar);
            BOOST_CHECK(values.at({"feedthrough", name + "_input"}) == expected);
            BOOST_CHECK(observed({"feedthrough", name + "_output"}) == expected);
        }
    }
    for (const auto& [name, expected] : arrays) {
        BOOST_TEST_CONTEXT(name) {
            BOOST_CHECK(values.at({"arrays", name}) == expected);
            BOOST_CHECK(observed({"arrays", name}) == expected);
        }
    }
}

std::string absolute_sources(const filesystem::path& fixture)
{
    auto xml = read_xml(fixture);
    for (auto pos = xml.find("source=\"../../"); pos != std::string::npos;
         pos = xml.find("source=\"../../", pos + 1)) {
        const auto begin = pos + std::string_view("source=\"").size();
        const auto end = xml.find('"', begin);
        const auto source = xml.substr(begin, end - begin);
        const auto uri = path_to_file_uri((fixture.parent_path() / source).lexically_normal());
        xml.replace(begin, source.size(), uri.view());
    }
    return xml;
}
} // namespace

BOOST_AUTO_TEST_CASE(osp_exact_scalars_and_arrays_reach_execution)
{
    auto resolver = default_model_uri_resolver();
    const auto config = load_osp_config(fixture_path("fmi3/system_structure/exact/OspSystemStructure.xml"), *resolver);
    check_exact_values(config.system_structure, config.initial_values);
}

BOOST_AUTO_TEST_CASE(ssp_exact_scalars_and_arrays_reach_execution)
{
    const auto config = ssp_loader{}.load(fixture_path("ssp/SSP20/SystemStructure.ssd"));
    check_exact_values(config.system_structure, config.parameter_sets.at(""));
    BOOST_CHECK_EQUAL(config.parameter_sets.at("scalars").size(), 14);
    BOOST_CHECK_EQUAL(config.parameter_sets.at("arrays").size(), 3);
}

BOOST_AUTO_TEST_CASE(osp_xml_clock_routes_fractional_schedule)
{
    auto resolver = default_model_uri_resolver();
    const auto config = load_osp_config(fixture_path("fmi3/system_structure/exact/OspClock.xml"), *resolver);
    const auto params = std::get<fixed_step_algorithm_params>(config.algorithm_configuration);
    execution exe(config.start_time, std::make_shared<fixed_step_algorithm>(params));
    auto observer = std::make_shared<last_value_observer>();
    exe.add_observer(observer);
    const auto entities = inject_system_structure(exe, config.system_structure, config.initial_values);
    const auto& ticks = config.system_structure.get_variable_description({"target", "updates"});
    const auto& snapshot = config.system_structure.get_variable_description({"target", "periodicArraySnapshot"});
    const auto sim = entities.simulators.at("target");
    BOOST_CHECK(exe.step() == to_duration(0.25));
    BOOST_CHECK(observer->get_variable_value(sim, ticks.type, ticks.reference) ==
        variable_value(integer_values{{2}}));
    BOOST_CHECK(observer->get_variable_value(sim, snapshot.type, snapshot.reference) ==
        variable_value(uint64_values{{42, std::numeric_limits<std::uint64_t>::max()}}, {2}));
    BOOST_CHECK(exe.step() == to_duration(0.5));
    BOOST_CHECK(observer->get_variable_value(sim, ticks.type, ticks.reference) ==
        variable_value(integer_values{{3}}));
}

BOOST_AUTO_TEST_CASE(ssp_exact_clock_declarations_route_events)
{
    const auto config = ssp_loader{}.load(fixture_path("ssp/SSP20/ClockRouting.ssd"));
    execution exe(config.start_time, std::make_shared<fixed_step_algorithm>(to_duration(1.5)));
    auto observer = std::make_shared<last_value_observer>();
    exe.add_observer(observer);
    const auto entities = inject_system_structure(exe, config.system_structure, config.parameter_sets.at(""));
    BOOST_REQUIRE(exe.simulate_until(to_time_point(2.0)));
    const auto& ticks = config.system_structure.get_variable_description({"target", "inClock2Ticks"});
    BOOST_CHECK(observer->get_variable_value(entities.simulators.at("target"), ticks.type, ticks.reference) ==
        variable_value(integer_values{{1}}));
}

BOOST_AUTO_TEST_CASE(osp_rejects_bad_versions_shapes_and_literals)
{
    utility::temp_dir dir;
    const auto path = dir.path() / "system.xml";
    auto resolver = default_model_uri_resolver();
    const auto osp = fixture_path("fmi3/system_structure/exact");
    const auto exact = absolute_sources(osp / "OspSystemStructure.xml");
    const auto clock = absolute_sources(osp / "OspClock.xml");
    const auto check = [&](const std::string& text, std::string_view reason) {
        write_xml(path, text);
        BOOST_CHECK_EXCEPTION(load_osp_config(path, *resolver), std::runtime_error,
            [&](const auto& e) { return std::string_view(e.what()).find(reason) != std::string_view::npos; });
    };
    check(replace_xml(exact, "version=\"0.2.0\"", "version=\"0.1\""), "Validation of ");
    check(replace_xml(exact, " version=\"0.2.0\"", ""), "missing OspSystemStructure version");
    check(replace_xml(exact, "version=\"0.2.0\"", "version=\"0.2\""), "unsupported OspSystemStructure version: 0.2");
    check(replace_xml(exact, "version=\"0.2.0\"", "version=\"9\""), "unsupported OspSystemStructure version");
    check(replace_xml(exact, "value=\"-128\"", "value=\"300\""), "does not fit");
    check(replace_xml(exact, "000AfeFF", "xyz"), "Validation of ");
    check(replace_xml(exact, "size=\"2\"", "size=\"3\""), "element count");
    check(replace_xml(exact, "<Enumeration value=\"2\"/>", "<Enumeration value=\"3\"/>"), "declared bounds");
    check(replace_xml(exact, "Float64_continuous_input\"><Real", "Float64_continuous_input\"><Float32"), "Cannot assign");
    check(replace_xml(clock, "resolution=\"4\"", "resolution=\"0\""), "positive intervalCounter and resolution");
    const auto oneTick = replace_xml(clock, "intervalCounter=\"2\" resolution=\"4\" shiftCounter=\"1\"",
        "intervalCounter=\"18446744073\" resolution=\"18446744073000000000\"");
    write_xml(path, oneTick);
    BOOST_CHECK_NO_THROW(load_osp_config(path, *resolver));
    check(replace_xml(oneTick, "18446744073000000000", "18446744073000000001"),
        "at least one simulation tick");
    check(replace_xml(clock, "intervalVariability=\"constant\"", "intervalVariability=\"changing\""), "Validation of ");
    check(replace_xml(clock, "intervalVariability=\"constant\"", "intervalVariability=\"countdown\""), "Validation of ");
    check(replace_xml(clock, "version=\"0.2.0\"", "version=\"0.1\""), "Validation of ");
    check(replace_xml(clock, "name=\"triggered\"", "name=\"periodic\""), "Clock");

    auto prefixed = replace_xml(exact, "xmlns=", "xmlns:osp=");
    for (auto at = prefixed.find('<', prefixed.find("?>") + 2); at != std::string::npos;
         at = prefixed.find('<', at + 1)) {
        prefixed.insert(at + (prefixed[at + 1] == '/' ? 2 : 1), "osp:");
    }
    write_xml(path, prefixed);
    BOOST_CHECK_EQUAL(load_osp_config(path, *resolver).initial_values.size(), 17);
}

BOOST_AUTO_TEST_CASE(ssp_rejects_bad_declarations_but_skips_bad_assignments)
{
    utility::temp_dir dir;
    const auto path = dir.path() / "system.ssd";
    const auto ssp = fixture_path("ssp/SSP20");
    const auto parameters = read_xml(ssp / "scalars.ssv");
    write_xml(dir.path() / "scalars.ssv", parameters);
    const auto exact = absolute_sources(ssp / "SystemStructure.ssd");
    const auto clock = absolute_sources(ssp / "ClockRouting.ssd");
    const auto check = [&](const std::string& text, std::string_view reason) {
        write_xml(path, text);
        BOOST_CHECK_EXCEPTION(ssp_loader{}.load(path), cosim::error,
            [&](const auto& e) { return std::string_view(e.what()).find(reason) != std::string_view::npos; });
    };
    const auto unsupported = [&](const std::string& text, std::string_view reason) {
        write_xml(path, text);
        BOOST_CHECK_EXCEPTION(ssp_loader{}.load(path), std::invalid_argument,
            [&](const auto& e) { return std::string_view(e.what()).find(reason) != std::string_view::npos; });
    };
    check(replace_xml(exact, "<c:Int8/>", "<c:UInt8/>"), "does not match FMU type");
    check(replace_xml(exact, "size=\"2\"", "size=\"3\""), "does not match the fixed FMU shape");
    check(replace_xml(clock, "priority=\"1\"", "priority=\"9\""), "does not match FMU Clock metadata");
    check(replace_xml(clock, "endConnector=\"inClock2\"/>",
        "endConnector=\"inClock2\"><ssc:LinearTransformation/></ssd:Connection>"), "requires scalar Real");
    unsupported(replace_xml(exact, "size=\"2\"", "sizeConnector=\"length\""), "sizeConnector");
    for (const auto* kind : {"BooleanMappingTransformation", "IntegerMappingTransformation",
             "EnumerationMappingTransformation"}) {
        unsupported(replace_xml(clock, "endConnector=\"inClock2\"/>",
            std::string("endConnector=\"inClock2\"><ssc:") + kind + "/></ssd:Connection>"), kind);
    }
    write_xml(path, exact);
    write_xml(dir.path() / "scalars.ssv",
        replace_xml(replace_xml(replace_xml(parameters, "-128", "300"), "000AfeFF", "not hex"), "Option 2", "unknown"));
    const auto config = ssp_loader{}.load(path);
    const auto& values = config.parameter_sets.at("scalars");
    BOOST_CHECK_EQUAL(values.size(), 11);
    BOOST_CHECK_EQUAL(values.count({"feedthrough", "Int8_input"}), 0);
    BOOST_CHECK_EQUAL(values.count({"feedthrough", "Binary_input"}), 0);
    BOOST_CHECK_EQUAL(values.count({"feedthrough", "Enumeration_input"}), 0);
}

BOOST_AUTO_TEST_SUITE_END()
