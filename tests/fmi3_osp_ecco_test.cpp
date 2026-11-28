#include <cosim/algorithm/ecco_algorithm.hpp>
#include <cosim/algorithm/fixed_step_algorithm.hpp>
#include <cosim/exception.hpp>
#include <cosim/fs_portability.hpp>
#include <cosim/manipulator/override_manipulator.hpp>
#include <cosim/observer/last_value_observer.hpp>
#include <cosim/orchestration.hpp>
#include <cosim/osp_config_parser.hpp>
#include <cosim/system_structure.hpp>
#include <cosim/time.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>


#include "test_require.hpp"


namespace
{

constexpr cosim::value_reference outputReference = 2;
constexpr cosim::value_reference inClock1Ticks = 2001;
constexpr cosim::value_reference result2Snapshot = 2008;
constexpr double fixedStepBaselineTolerance = 0.02;


struct configured_system
{
    cosim::system_structure structure;
    cosim::variable_value_map initialValues;
};


struct power_bond_result
{
    std::vector<cosim::duration> stepSizes;
    cosim::time_point endTime;
    double outputA;
    double outputB;
};


struct clocked_result
{
    std::vector<cosim::duration> stepSizes;
    double outputA;
    double outputB;
    int sourceTicks;
    int targetResult;
    cosim::variable_value binary;
    cosim::variable_value enumeration;
};


struct output_pair
{
    double outputA;
    double outputB;
};


std::shared_ptr<cosim::model> load_model(
    const cosim::filesystem::path& fixtureDirectory,
    const char* filename)
{
    auto resolver = cosim::default_model_uri_resolver();
    return resolver->lookup_model(cosim::path_to_file_uri(
        fixtureDirectory / "fmus" / filename));
}


configured_system make_power_bond_system(
    const cosim::filesystem::path& fixtureDirectory)
{
    configured_system configured;
    const auto model = load_model(fixtureDirectory, "PowerBond.fmu");
    configured.structure.add_entity("componentA", model);
    configured.structure.add_entity("componentB", model);
    configured.structure.connect_variables(
        {"componentB", "output"},
        {"componentA", "input"});
    configured.structure.connect_variables(
        {"componentA", "output"},
        {"componentB", "input"});
    configured.structure.add_power_bond(
        "mechanicalBond",
        {{"componentA", "input"},
            {"componentA", "output"},
            {"componentB", "input"},
            {"componentB", "output"}});

    cosim::add_variable_value(
        configured.initialValues,
        configured.structure,
        {"componentA", "gain"},
        0.2);
    cosim::add_variable_value(
        configured.initialValues,
        configured.structure,
        {"componentA", "bias"},
        1.0);
    cosim::add_variable_value(
        configured.initialValues,
        configured.structure,
        {"componentB", "gain"},
        0.3);
    cosim::add_variable_value(
        configured.initialValues,
        configured.structure,
        {"componentB", "bias"},
        -0.5);
    return configured;
}


configured_system make_clocked_power_bond_system(
    const cosim::filesystem::path& fixtureDirectory)
{
    auto configured = make_power_bond_system(fixtureDirectory);
    const auto clockModel =
        load_model(fixtureDirectory, "ClockedCoSimulation.fmu");
    const auto dataModel = load_model(fixtureDirectory, "Feedthrough.fmu");
    configured.structure.add_entity("clockSource", clockModel);
    configured.structure.add_entity("clockTarget", clockModel);
    configured.structure.add_entity("dataSource", dataModel);
    configured.structure.add_entity("dataTarget", dataModel);
    configured.structure.connect_variables(
        {"clockSource", "outClock"},
        {"clockTarget", "inClock2"});
    configured.structure.connect_variables(
        {"clockSource", "inClock1Ticks"},
        {"clockTarget", "input2"});
    configured.structure.connect_variables(
        {"dataSource", "Binary_output"},
        {"dataTarget", "Binary_input"});
    configured.structure.connect_variables(
        {"dataSource", "Enumeration_output"},
        {"dataTarget", "Enumeration_input"});
    return configured;
}


double observed_real(
    cosim::last_value_observer& observer,
    cosim::simulator_index simulator,
    cosim::value_reference reference)
{
    double value = 0.0;
    observer.get_real(
        simulator,
        gsl::make_span(&reference, 1),
        gsl::make_span(&value, 1));
    return value;
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


void require_finite(const std::vector<double>& values)
{
    REQUIRE(!values.empty());
    REQUIRE(std::all_of(
        values.begin(),
        values.end(),
        [](double value) { return std::isfinite(value); }));
}


power_bond_result run_power_bond(
    const cosim::system_structure& structure,
    const cosim::variable_value_map& initialValues,
    cosim::time_point startTime,
    cosim::ecco_algorithm_params parameters)
{
    auto algorithm = std::make_shared<cosim::ecco_algorithm>(parameters);
    auto execution = cosim::execution(startTime, algorithm);
    auto observer = std::make_shared<cosim::last_value_observer>();
    execution.add_observer(observer);
    const auto maps =
        cosim::inject_system_structure(execution, structure, initialValues);

    power_bond_result result;
    for (int step = 0; step < 10; ++step) {
        const auto stepSize = execution.step();
        REQUIRE(stepSize >= parameters.min_step_size);
        REQUIRE(stepSize <= parameters.max_step_size);
        result.stepSizes.push_back(stepSize);
    }
    result.endTime = execution.current_time();
    result.outputA = observed_real(
        *observer, maps.simulators.at("componentA"), outputReference);
    result.outputB = observed_real(
        *observer, maps.simulators.at("componentB"), outputReference);
    REQUIRE(std::isfinite(result.outputA));
    REQUIRE(std::isfinite(result.outputB));

    require_finite(algorithm->get_powerbond_energies(
        maps.simulators.at("componentA")));
    require_finite(algorithm->get_powerbond_energies(
        maps.simulators.at("componentB")));
    REQUIRE(
        std::set<cosim::duration>(
            result.stepSizes.begin(),
            result.stepSizes.end())
            .size() >= 2U);
    return result;
}


output_pair run_fixed_power_bond(
    const cosim::system_structure& structure,
    const cosim::variable_value_map& initialValues,
    cosim::time_point startTime,
    cosim::time_point endTime,
    cosim::duration stepSize)
{
    auto execution = cosim::execution(
        startTime,
        std::make_shared<cosim::fixed_step_algorithm>(stepSize));
    auto observer = std::make_shared<cosim::last_value_observer>();
    execution.add_observer(observer);
    const auto maps =
        cosim::inject_system_structure(execution, structure, initialValues);
    while (execution.current_time() < endTime) {
        REQUIRE(execution.step() == stepSize);
    }
    REQUIRE(execution.current_time() == endTime);
    return {
        observed_real(
            *observer, maps.simulators.at("componentA"), outputReference),
        observed_real(
            *observer, maps.simulators.at("componentB"), outputReference)};
}


void require_equal_results(
    const power_bond_result& first,
    const power_bond_result& second)
{
    REQUIRE(first.stepSizes == second.stepSizes);
    REQUIRE(first.endTime == second.endTime);
    REQUIRE(std::abs(first.outputA - second.outputA) < 1e-12);
    REQUIRE(std::abs(first.outputB - second.outputB) < 1e-12);
}


clocked_result run_clocked_power_bond(
    const cosim::system_structure& structure,
    const cosim::variable_value_map& initialValues,
    cosim::time_point startTime,
    std::shared_ptr<cosim::algorithm> algorithm)
{
    auto execution = cosim::execution(startTime, algorithm);
    auto observer = std::make_shared<cosim::last_value_observer>();
    auto manipulator = std::make_shared<cosim::override_manipulator>();
    execution.add_observer(observer);
    execution.add_manipulator(manipulator);
    const auto maps =
        cosim::inject_system_structure(execution, structure, initialValues);

    const cosim::variable_value binary(
        cosim::binary_values{{
            cosim::binary_value{{0x00, 0x42, 0xff}}}});
    const cosim::variable_value enumeration(
        cosim::enumeration_values{{cosim::enumeration_value{2}}});
    manipulator->override_variable(
        maps.simulators.at("dataSource"),
        cosim::variable_type::binary,
        32,
        binary);
    manipulator->override_variable(
        maps.simulators.at("dataSource"),
        cosim::variable_type::enumeration,
        34,
        enumeration);

    clocked_result result;
    for (int step = 0; step < 3; ++step) {
        result.stepSizes.push_back(execution.step());
        REQUIRE(execution.current_time() == cosim::to_time_point(step + 1.0));
    }

    result.outputA = observed_real(
        *observer, maps.simulators.at("componentA"), outputReference);
    result.outputB = observed_real(
        *observer, maps.simulators.at("componentB"), outputReference);
    result.sourceTicks = observed_integer(
        *observer, maps.simulators.at("clockSource"), inClock1Ticks);
    result.targetResult = observed_integer(
        *observer, maps.simulators.at("clockTarget"), result2Snapshot);
    result.binary = observer->get_variable_value(
        maps.simulators.at("dataTarget"),
        cosim::variable_type::binary,
        31);
    result.enumeration = observer->get_variable_value(
        maps.simulators.at("dataTarget"),
        cosim::variable_type::enumeration,
        33);

    REQUIRE(result.stepSizes ==
        std::vector<cosim::duration>(3, cosim::to_duration(1.0)));
    REQUIRE(result.sourceTicks == 4);
    REQUIRE(result.targetResult == 3);
    REQUIRE(result.binary == binary);
    REQUIRE(result.enumeration == enumeration);
    REQUIRE(std::isfinite(result.outputA));
    REQUIRE(std::isfinite(result.outputB));
    return result;
}


void require_equal_results(
    const clocked_result& first,
    const clocked_result& second)
{
    REQUIRE(first.stepSizes == second.stepSizes);
    REQUIRE(std::abs(first.outputA - second.outputA) < 1e-12);
    REQUIRE(std::abs(first.outputB - second.outputB) < 1e-12);
    REQUIRE(first.sourceTicks == second.sourceTicks);
    REQUIRE(first.targetResult == second.targetResult);
    REQUIRE(first.binary == second.binary);
    REQUIRE(first.enumeration == second.enumeration);
}


void require_invalid_power_bond_xml(
    const cosim::filesystem::path& fixtureDirectory,
    const char* filename)
{
    bool rejected = false;
    try {
        auto resolver = cosim::default_model_uri_resolver();
        (void)cosim::load_osp_config(
            fixtureDirectory / "invalid" / filename,
            *resolver);
    } catch (const cosim::error& error) {
        rejected = error.code() ==
            cosim::make_error_code(cosim::errc::invalid_system_structure);
    }
    REQUIRE(rejected);
}


void test_power_bond_xml_and_api(
    const cosim::filesystem::path& fixtureDirectory)
{
    auto resolver = cosim::default_model_uri_resolver();
    const auto xml = cosim::load_osp_config(
        fixtureDirectory / "ecco" / "power_bond" /
            "OspSystemStructure.xml",
        *resolver);
    const auto xmlParameters = std::get<cosim::ecco_algorithm_params>(
        xml.algorithm_configuration);
    const auto xmlResult = run_power_bond(
        xml.system_structure,
        xml.initial_values,
        xml.start_time,
        xmlParameters);
    const auto elapsed = xmlResult.endTime - xml.start_time;
    REQUIRE(elapsed.count() % 8 == 0);
    const auto baselineStep = cosim::duration(elapsed.count() / 8);
    REQUIRE(baselineStep < xmlParameters.step_size);
    const auto baseline = run_fixed_power_bond(
        xml.system_structure,
        xml.initial_values,
        xml.start_time,
        xmlResult.endTime,
        baselineStep);
    // Eight smaller fixed steps reach the exact adaptive endpoint. The
    // deterministic algebraic fixture stays within this documented tolerance
    // despite the different communication-point histories.
    REQUIRE(
        std::abs(xmlResult.outputA - baseline.outputA) <
        fixedStepBaselineTolerance);
    REQUIRE(
        std::abs(xmlResult.outputB - baseline.outputB) <
        fixedStepBaselineTolerance);

    const auto api = make_power_bond_system(fixtureDirectory);
    const auto apiResult = run_power_bond(
        api.structure,
        api.initialValues,
        cosim::to_time_point(0.0),
        xmlParameters);
    require_equal_results(xmlResult, apiResult);
}


void test_clocked_power_bond_xml_and_api(
    const cosim::filesystem::path& fixtureDirectory)
{
    auto resolver = cosim::default_model_uri_resolver();
    const auto xml = cosim::load_osp_config(
        fixtureDirectory / "ecco" / "power_bond_clocks" /
            "OspSystemStructure.xml",
        *resolver);
    const auto xmlParameters = std::get<cosim::ecco_algorithm_params>(
        xml.algorithm_configuration);
    const auto xmlResult = run_clocked_power_bond(
        xml.system_structure,
        xml.initial_values,
        xml.start_time,
        std::make_shared<cosim::ecco_algorithm>(xmlParameters));

    const auto api = make_clocked_power_bond_system(fixtureDirectory);
    const auto apiResult = run_clocked_power_bond(
        api.structure,
        api.initialValues,
        cosim::to_time_point(0.0),
        std::make_shared<cosim::ecco_algorithm>(xmlParameters));
    require_equal_results(xmlResult, apiResult);

    // At the common Clock instants, ECCO and a one-second fixed-step baseline
    // must produce the same triggered ticks, clocked data, side-channel values,
    // and power-bond outputs.
    const auto fixedResult = run_clocked_power_bond(
        xml.system_structure,
        xml.initial_values,
        xml.start_time,
        std::make_shared<cosim::fixed_step_algorithm>(
            cosim::to_duration(1.0)));
    require_equal_results(xmlResult, fixedResult);
}


} // namespace


int main()
{
    try {
        const auto* fixtureDirectory =
            std::getenv("FMI3_SYSTEM_STRUCTURE_DIR");
        REQUIRE(fixtureDirectory);
        const auto fixturePath = cosim::filesystem::path(fixtureDirectory);

        test_power_bond_xml_and_api(fixturePath);
        test_clocked_power_bond_xml_and_api(fixturePath);
        require_invalid_power_bond_xml(
            fixturePath, "non_float64_power_bond.xml");
        require_invalid_power_bond_xml(
            fixturePath, "clock_power_bond.xml");
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
