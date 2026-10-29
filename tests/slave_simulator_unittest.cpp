#define BOOST_TEST_MODULE cosim::slave_simulator unittest
#include <cosim/exception.hpp>
#include <cosim/algorithm/ecco_algorithm.hpp>
#include <cosim/algorithm/fixed_step_algorithm.hpp>
#include <cosim/fmi/importer.hpp>
#include <cosim/fmi/fmu.hpp>
#include <cosim/fs_portability.hpp>
#include <cosim/manipulator/override_manipulator.hpp>
#include <cosim/manipulator/scenario_manager.hpp>
#include <cosim/observer/last_value_observer.hpp>
#include <cosim/observer/file_observer.hpp>
#include <cosim/observer/time_series_observer.hpp>
#include <cosim/slave_simulator.hpp>
#include <cosim/system_structure.hpp>
#include <cosim/utility/filesystem.hpp>

#include <boost/test/unit_test.hpp>

#include <fstream>
#include <iterator>

#include "mock_slave.hpp"

namespace
{
class enumeration_mock_slave : public mock_slave
{
public:
    cosim::model_description model_description() const override
    {
        auto model = mock_slave::model_description();
        cosim::variable_description input{};
        input.name = "enumerationIn";
        input.reference = 100;
        input.type = cosim::variable_type::enumeration;
        input.causality = cosim::variable_causality::input;
        input.variability = cosim::variable_variability::discrete;
        model.variables.push_back(input);
        input.name = "enumerationOut";
        input.reference = 101;
        input.causality = cosim::variable_causality::output;
        model.variables.push_back(input);
        return model;
    }

    std::vector<cosim::variable_value> get_variable_values(
        gsl::span<const cosim::typed_value_reference> variables) const override
    {
        std::vector<cosim::variable_value> values;
        for (const auto variable : variables) {
            if (variable.type != cosim::variable_type::enumeration ||
                (variable.reference != 100 && variable.reference != 101)) {
                throw std::invalid_argument("Unknown mock Enumeration reference");
            }
            values.emplace_back(cosim::enumeration_values{{value}});
        }
        return values;
    }

    void set_variable_values(
        gsl::span<const cosim::typed_value_reference> variables,
        gsl::span<const cosim::variable_value> values) override
    {
        if (variables.size() != values.size()) throw std::invalid_argument("Count mismatch");
        for (std::size_t i = 0; i < variables.size(); ++i) {
            if (variables[i].type != cosim::variable_type::enumeration ||
                variables[i].reference != 100 ||
                values[i].type() != cosim::variable_type::enumeration ||
                !values[i].is_scalar()) {
                throw std::invalid_argument("Invalid mock Enumeration assignment");
            }
            value = std::get<cosim::enumeration_values>(values[i].values()).values[0];
            ++assignments;
        }
    }

    state_index import_state(const cosim::serialization::node& exported) override
    {
        ++stateImports;
        return mock_slave::import_state(exported);
    }

    cosim::enumeration_value value{1};
    int assignments = 0;
    int stateImports = 0;
};

class started_enumeration_mock_slave : public enumeration_mock_slave
{
public:
    cosim::model_description model_description() const override
    {
        auto model = enumeration_mock_slave::model_description();
        for (auto& variable : model.variables) {
            if (variable.name == "enumerationIn") {
                variable.start = cosim::enumeration_value{1};
            }
        }
        return model;
    }
};

class enumeration_mock_model : public cosim::model
{
public:
    explicit enumeration_mock_model(std::shared_ptr<enumeration_mock_slave> instance)
        : instance_(std::move(instance)) {}
    std::shared_ptr<const cosim::model_description> description() const noexcept override
    {
        return std::make_shared<cosim::model_description>(instance_->model_description());
    }
    std::shared_ptr<cosim::slave> instantiate(std::string_view) override
    {
        return instance_;
    }
private:
    std::shared_ptr<enumeration_mock_slave> instance_;
};

class enumeration_identity_function : public cosim::function
{
public:
    cosim::function_description description() const override
    {
        cosim::function_description result;
        result.io_groups = {
            {"in", 1, {{"", cosim::variable_type::enumeration, cosim::variable_causality::input, 1}}},
            {"out", 1, {{"", cosim::variable_type::enumeration, cosim::variable_causality::output, 1}}}};
        return result;
    }
    void set_real(const cosim::function_io_reference&, double) override { throw std::logic_error("Invalid type"); }
    void set_integer(const cosim::function_io_reference&, int) override { throw std::logic_error("Invalid type"); }
    void set_boolean(const cosim::function_io_reference&, bool) override { throw std::logic_error("Invalid type"); }
    void set_string(const cosim::function_io_reference&, std::string_view) override { throw std::logic_error("Invalid type"); }
    double get_real(const cosim::function_io_reference&) const override { throw std::logic_error("Invalid type"); }
    int get_integer(const cosim::function_io_reference&) const override { throw std::logic_error("Invalid type"); }
    bool get_boolean(const cosim::function_io_reference&) const override { throw std::logic_error("Invalid type"); }
    std::string_view get_string(const cosim::function_io_reference&) const override { throw std::logic_error("Invalid type"); }
    void set_variable_value(
        const cosim::function_io_reference& reference, const cosim::variable_value& value) override
    {
        if (reference != cosim::function_io_reference{0, 0, 0, 0} ||
            value.type() != cosim::variable_type::enumeration || !value.is_scalar()) {
            throw std::invalid_argument("Invalid Enumeration function input");
        }
        input_ = std::get<cosim::enumeration_values>(value.values()).values[0];
    }
    cosim::variable_value get_variable_value(const cosim::function_io_reference& reference) const override
    {
        if (reference != cosim::function_io_reference{1, 0, 0, 0}) {
            throw std::invalid_argument("Invalid Enumeration function output");
        }
        return cosim::variable_value(cosim::enumeration_values{{output_}});
    }
    void calculate() override { output_ = input_; }

private:
    cosim::enumeration_value input_{1};
    cosim::enumeration_value output_{1};
};
} // namespace

#include "connection_type_tests.hpp"
#include "default_slave_batch_tests.hpp"
#include "modifier_reporting_tests.hpp"

BOOST_AUTO_TEST_CASE(enumeration_initial_value_injection)
{
    auto slave = std::make_shared<enumeration_mock_slave>();
    auto model = std::make_shared<enumeration_mock_model>(slave);
    auto target = std::make_shared<enumeration_mock_slave>();
    auto targetModel = std::make_shared<enumeration_mock_model>(target);
    cosim::system_structure system;
    system.add_entity("source", model);
    system.add_entity("target", targetModel);
    system.connect_variables(
        {"source", "enumerationOut"}, {"target", "enumerationIn"});
    cosim::variable_value_map initial;
    cosim::add_variable_value(initial, system, {"source", "enumerationIn"}, 2);
    const auto& stored = initial.at({"source", "enumerationIn"});
    BOOST_TEST(cosim::type_of(stored) == cosim::variable_type::enumeration);
    BOOST_TEST(std::get<cosim::enumeration_value>(stored).value == 2);
    BOOST_TEST(cosim::type_of(cosim::scalar_value(2)) == cosim::variable_type::integer);
    auto execution = cosim::execution(
        cosim::time_point(),
        std::make_shared<cosim::fixed_step_algorithm>(cosim::to_duration(0.1)));
    auto observer = std::make_shared<cosim::last_value_observer>();
    execution.add_observer(observer);
    const auto entities = cosim::inject_system_structure(execution, system, initial);
    BOOST_CHECK(execution.simulate_until(cosim::to_time_point(0.2)));
    BOOST_TEST(slave->value.value == 2);
    BOOST_TEST(target->value.value == 2);
    const auto observed = observer->get_variable_value(
        entities.simulators.at("target"), cosim::variable_type::enumeration, 101);
    BOOST_TEST(std::get<cosim::enumeration_values>(observed.values()).values[0].value == 2);
}

BOOST_AUTO_TEST_CASE(enumeration_function_and_algorithm_connections)
{
    const auto testConnections = [](std::shared_ptr<cosim::algorithm> algorithm) {
        auto source = std::make_shared<enumeration_mock_slave>();
        auto direct = std::make_shared<enumeration_mock_slave>();
        auto throughFunction = std::make_shared<enumeration_mock_slave>();
        auto execution = cosim::execution(cosim::time_point(), std::move(algorithm));
        const auto sourceId = execution.add_slave(source, "source");
        const auto directId = execution.add_slave(direct, "direct");
        const auto targetId = execution.add_slave(throughFunction, "function-target");
        const auto functionId = execution.add_function(std::make_shared<enumeration_identity_function>());
        const auto sourceOutput = cosim::variable_id{sourceId, cosim::variable_type::enumeration, 101};
        execution.connect_variables(
            sourceOutput, cosim::variable_id{directId, cosim::variable_type::enumeration, 100});
        execution.connect_variables(
            sourceOutput, cosim::function_io_id{functionId, cosim::variable_type::enumeration, {0, 0, 0, 0}});
        execution.connect_variables(
            cosim::function_io_id{functionId, cosim::variable_type::enumeration, {1, 0, 0, 0}},
            cosim::variable_id{targetId, cosim::variable_type::enumeration, 100});
        execution.set_variable_initial_value(
            cosim::variable_id{sourceId, cosim::variable_type::enumeration, 100},
            cosim::variable_value(cosim::integer_values{{2}}));
        BOOST_REQUIRE(execution.simulate_until(cosim::to_time_point(0.1)));
        BOOST_TEST(direct->value.value == 2);
        BOOST_TEST(throughFunction->value.value == 2);
    };
    testConnections(std::make_shared<cosim::fixed_step_algorithm>(cosim::to_duration(0.1)));
    testConnections(std::make_shared<cosim::ecco_algorithm>(cosim::ecco_algorithm_params{
        0.99, cosim::to_duration(0.1), cosim::to_duration(0.01), cosim::to_duration(0.1),
        0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15}));
}

BOOST_AUTO_TEST_CASE(simulator_enumeration_value_cache)
{
    auto slave = std::make_shared<enumeration_mock_slave>();
    cosim::slave_simulator simulator(slave, "enumeration");
    simulator.expose_for_setting(cosim::variable_type::enumeration, 100);
    simulator.expose_for_getting(cosim::variable_type::enumeration, 101);
    simulator.setup(cosim::time_point(), {}, {});
    BOOST_TEST(slave->assignments == 0);
    simulator.set_variable_value(
        cosim::variable_type::enumeration, 100,
        cosim::variable_value(cosim::integer_values{{2}}));
    const auto saved = simulator.save_state();
    const auto exported = simulator.export_state(saved);
    const auto imported = simulator.import_state(exported);
    simulator.restore_state(imported);
    simulator.do_iteration();
    BOOST_TEST(slave->assignments == 1);
    const auto value =
        simulator.get_variable_value(cosim::variable_type::enumeration, 101);
    BOOST_TEST(value.type() == cosim::variable_type::enumeration);
    BOOST_TEST(std::get<cosim::enumeration_values>(value.values()).values[0].value == 2);
    simulator.do_iteration();
    BOOST_TEST(slave->assignments == 1);
    BOOST_CHECK_THROW(simulator.set_variable_value(
        cosim::variable_type::enumeration, 100,
        cosim::variable_value(cosim::integer_values{{1, 2}}, {2})),
        std::invalid_argument);
}

BOOST_AUTO_TEST_CASE(enumeration_time_series_and_file_observation)
{
    cosim::utility::temp_dir directory;
    {
        auto slave = std::make_shared<started_enumeration_mock_slave>();
        auto execution = cosim::execution(
            cosim::time_point(),
            std::make_shared<cosim::fixed_step_algorithm>(cosim::to_duration(0.1)));
        auto series = std::make_shared<cosim::time_series_observer>();
        auto file = std::make_shared<cosim::file_observer>(directory.path());
        execution.add_observer(series);
        execution.add_observer(file);
        const auto sim = execution.add_slave(slave, "enumeration");
        series->start_observing({sim, cosim::variable_type::enumeration, 101});
        BOOST_REQUIRE(execution.simulate_until(cosim::to_time_point(0.2)));
        cosim::variable_value values[4];
        cosim::step_number steps[4];
        cosim::time_point times[4];
        const auto count = series->get_variable_samples(
            sim, cosim::variable_type::enumeration, 101, 0,
            values, steps, times);
        BOOST_REQUIRE_GT(count, 0U);
        BOOST_TEST(values[0].type() == cosim::variable_type::enumeration);
        BOOST_TEST(std::get<cosim::enumeration_values>(values[0].values()).values[0].value == 1);
        series->stop_observing({sim, cosim::variable_type::enumeration, 101});
    }
    bool sawCsv = false;
    bool sawMetadata = false;
    for (const auto& entry : cosim::filesystem::directory_iterator(directory.path())) {
        std::ifstream input(entry.path());
        const std::string contents(
            (std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (entry.path().extension() == ".csv") {
            sawCsv = contents.find("enumerationOut") != std::string::npos &&
                contents.find(",1,1\n") != std::string::npos;
        } else if (entry.path().extension() == ".yaml") {
            sawMetadata = contents.find("enumerationIn") != std::string::npos &&
                contents.find("start value:  1") != std::string::npos;
        }
    }
    BOOST_TEST(sawCsv);
    BOOST_TEST(sawMetadata);
}

BOOST_AUTO_TEST_CASE(enumeration_modifiers)
{
    auto slave = std::make_shared<enumeration_mock_slave>();
    cosim::slave_simulator simulator(slave, "enumeration");
    simulator.expose_for_setting(cosim::variable_type::enumeration, 100);
    simulator.expose_for_getting(cosim::variable_type::enumeration, 101);
    simulator.setup(cosim::time_point(), {}, {});
    simulator.set_variable_value(
        cosim::variable_type::enumeration, 100,
        cosim::variable_value(cosim::enumeration_values{{cosim::enumeration_value{1}}}));
    simulator.set_variable_input_modifier(
        cosim::variable_type::enumeration, 100,
        [](const cosim::variable_value&, cosim::duration) {
            return cosim::variable_value(cosim::integer_values{{2}});
        });
    simulator.set_variable_output_modifier(
        cosim::variable_type::enumeration, 101,
        [](const cosim::variable_value&, cosim::duration) {
            return cosim::variable_value(cosim::integer_values{{1}});
        });
    BOOST_CHECK_THROW(simulator.save_state(), cosim::error);
    simulator.do_iteration();
    BOOST_TEST(slave->value.value == 2);
    const auto modified = simulator.get_variable_value(cosim::variable_type::enumeration, 101);
    BOOST_TEST(modified.type() == cosim::variable_type::enumeration);
    BOOST_TEST(std::get<cosim::enumeration_values>(modified.values()).values[0].value == 1);
    simulator.set_variable_input_modifier(cosim::variable_type::enumeration, 100, nullptr);
    simulator.set_variable_output_modifier(cosim::variable_type::enumeration, 101, nullptr);
    simulator.do_iteration();
    BOOST_TEST(slave->value.value == 1);
    simulator.set_variable_output_modifier(
        cosim::variable_type::enumeration, 101,
        [](const cosim::variable_value&, cosim::duration) {
            return cosim::variable_value(cosim::integer_values{{1, 2}}, {2});
        });
    BOOST_CHECK_THROW(simulator.do_iteration(), cosim::error);
}

BOOST_AUTO_TEST_CASE(enumeration_missing_start_and_legacy_state)
{
    auto slave = std::make_shared<enumeration_mock_slave>();
    cosim::slave_simulator simulator(slave, "enumeration");
    simulator.expose_for_setting(cosim::variable_type::enumeration, 100);
    simulator.expose_for_getting(cosim::variable_type::enumeration, 101);
    simulator.setup(cosim::time_point(), {}, {});
    const auto saved = simulator.save_state();
    auto exported = simulator.export_state(saved);
    auto malformed = exported;
    malformed.put("enumeration_set_cache.100.pending", true);
    BOOST_CHECK_THROW(simulator.import_state(malformed), cosim::error);
    BOOST_TEST(slave->stateImports == 0);
    malformed = exported;
    malformed.erase("enumeration_get_cache");
    BOOST_CHECK_THROW(simulator.import_state(malformed), cosim::error);
    BOOST_TEST(slave->stateImports == 0);
    exported.erase("enumeration_get_cache");
    exported.erase("enumeration_set_cache");
    const auto imported = simulator.import_state(exported);
    simulator.restore_state(imported);
    simulator.do_iteration();
    BOOST_TEST(slave->assignments == 0);
    BOOST_TEST(std::get<cosim::enumeration_values>(
        simulator.get_variable_value(cosim::variable_type::enumeration, 101).values()).values[0].value == 1);
    simulator.set_variable_input_modifier(
        cosim::variable_type::enumeration, 100,
        [](const cosim::variable_value& original, cosim::duration) { return original; });
    BOOST_CHECK_THROW(simulator.do_iteration(), cosim::error);
    BOOST_CHECK_THROW(simulator.save_state(), cosim::error);
    simulator.set_variable_input_modifier(cosim::variable_type::enumeration, 100, nullptr);
    simulator.do_iteration();
    BOOST_TEST(slave->assignments == 0);
}

BOOST_AUTO_TEST_CASE(enumeration_scenario_modifier)
{
    auto slave = std::make_shared<enumeration_mock_slave>();
    cosim::slave_simulator simulator(slave, "enumeration");
    simulator.expose_for_setting(cosim::variable_type::enumeration, 100);
    simulator.setup(cosim::time_point(), {}, {});
    simulator.set_variable_value(
        cosim::variable_type::enumeration, 100,
        cosim::variable_value(cosim::integer_values{{1}}));
    cosim::scenario_manager manager;
    manager.simulator_added(0, &simulator, cosim::time_point());
    cosim::scenario::scenario scenario;
    scenario.events.push_back({cosim::time_point(), {0, 100,
        cosim::scenario::variable_modifier{cosim::variable_type::enumeration,
            [](const cosim::variable_value&, cosim::duration) {
                return cosim::variable_value(cosim::integer_values{{2}});
            }}, true}});
    manager.load_scenario(scenario, cosim::time_point());
    manager.step_commencing(cosim::time_point());
    simulator.do_iteration();
    BOOST_TEST(slave->value.value == 2);
    manager.abort_scenario();
    simulator.do_iteration();
    BOOST_TEST(slave->value.value == 1);
}

BOOST_AUTO_TEST_CASE(enumeration_override_and_reset)
{
    auto slave = std::make_shared<enumeration_mock_slave>();
    cosim::slave_simulator simulator(slave, "enumeration");
    simulator.expose_for_setting(cosim::variable_type::enumeration, 100);
    simulator.setup(cosim::time_point(), {}, {});
    simulator.set_variable_value(
        cosim::variable_type::enumeration, 100,
        cosim::variable_value(cosim::integer_values{{1}}));
    cosim::override_manipulator manipulator;
    manipulator.simulator_added(0, &simulator, cosim::time_point());
    manipulator.override_variable(
        0, cosim::variable_type::enumeration, 100,
        cosim::variable_value(cosim::integer_values{{2}}));
    manipulator.step_commencing(cosim::time_point());
    simulator.do_iteration();
    BOOST_TEST(slave->value.value == 2);
    manipulator.reset_variable(0, cosim::variable_type::enumeration, 100);
    manipulator.step_commencing(cosim::time_point());
    simulator.do_iteration();
    BOOST_TEST(slave->value.value == 1);
}

BOOST_AUTO_TEST_CASE(slave_default_whole_value_bridges)
{
    mock_slave slave;
    const cosim::typed_value_reference variables[] = {
        {cosim::variable_type::real, mock_slave::real_in_reference},
        {cosim::variable_type::integer, mock_slave::integer_in_reference},
        {cosim::variable_type::boolean, mock_slave::boolean_in_reference},
        {cosim::variable_type::string, mock_slave::string_in_reference}};
    const cosim::variable_value values[] = {
        cosim::variable_value(cosim::real_values{{2.5}}),
        cosim::variable_value(cosim::integer_values{{-3}}),
        cosim::variable_value(cosim::boolean_values{{true}}),
        cosim::variable_value(cosim::string_values{{"bridge"}})};

    slave.set_variable_values(variables, values);
    const auto retrieved = slave.get_variable_values(variables);
    BOOST_REQUIRE_EQUAL(retrieved.size(), 4);
    for (std::size_t i = 0; i < retrieved.size(); ++i) {
        BOOST_CHECK(retrieved[i] == values[i]);
    }

    const cosim::typed_value_reference enumeration{
        cosim::variable_type::enumeration, 1};
    const cosim::variable_value enumerationValue(
        cosim::enumeration_values{{cosim::enumeration_value{1}}});
    BOOST_CHECK_THROW(
        slave.get_variable_values(gsl::make_span(&enumeration, 1)), cosim::error);
    BOOST_CHECK_THROW(
        slave.set_variable_values(
            gsl::make_span(&enumeration, 1), gsl::make_span(&enumerationValue, 1)),
        cosim::error);
    BOOST_CHECK_THROW(
        slave.set_variable_values(
            gsl::make_span(variables, 1), gsl::span<const cosim::variable_value>()),
        std::invalid_argument);
    const cosim::variable_value array(cosim::real_values{{1.0, 2.0}}, {2});
    BOOST_CHECK_THROW(
        slave.set_variable_values(
            gsl::make_span(variables, 1), gsl::make_span(&array, 1)), cosim::error);
}


BOOST_AUTO_TEST_CASE(slave_simulator_save_state)
{
    const auto referenceFmuDir = std::getenv("REFERENCE_FMU_V2_DIR");
    BOOST_TEST_REQUIRE(!!referenceFmuDir);
    auto importer = cosim::fmi::importer::create();
    const std::string modelName = "Dahlquist";
    auto fmu = importer->import(
        cosim::filesystem::path(referenceFmuDir) / (modelName + ".fmu"));
    const auto modelDescription = fmu->model_description();
    BOOST_TEST(modelDescription->uuid == "{221063D2-EF4A-45FE-B954-B5BFEEA9A59B}");

    const auto xVar = cosim::find_variable(*modelDescription, "x")->reference;

    auto t = cosim::time_point();
    const auto dt = cosim::to_duration(1.0);

    auto sim = cosim::slave_simulator(
        fmu->instantiate_slave("testSlave"),
        "testSlave");
    sim.expose_for_getting(cosim::variable_type::real, xVar);

    sim.setup(t, {}, {});
    const auto value0 = sim.get_real(xVar);
    BOOST_TEST(value0 == 1.0);
    const auto state0 = sim.save_state();
    const auto exportedState0 = sim.export_state(state0);
    BOOST_TEST(exportedState0.get_child("state").get<int>("lifecycle_state") == 1);

    sim.start_simulation();
    sim.do_step(t, dt);
    t += dt;
    const auto value1 = sim.get_real(xVar);
    BOOST_TEST((0.0 < value1 && value1 < value0));
    const auto state1 = sim.save_state();
    const auto exportedState1 = sim.export_state(state1);
    const auto& exportedFmuState = exportedState1.get_child("state");
    BOOST_TEST(exportedFmuState.get<int>("scheme_version") == 1);
    BOOST_TEST(exportedFmuState.get<int>("lifecycle_state") == 2);
    BOOST_TEST(!exportedFmuState.get_child_optional("setup_complete"));
    BOOST_TEST(!exportedFmuState.get_child_optional("simulation_started"));

    auto incompatibleState = exportedState1;
    incompatibleState.get_child("state").put("scheme_version", 0);
    BOOST_CHECK_EXCEPTION(
        sim.import_state(incompatibleState),
        cosim::error,
        [](const cosim::error& error) {
            return error.code() == cosim::make_error_code(cosim::errc::bad_file);
        });
    sim.release_state(state1);

    sim.do_step(t, dt);
    t += dt;
    const auto value2 = sim.get_real(xVar);
    BOOST_TEST((0.0 < value2 && value2 < value1));
    const auto state2 = sim.save_state();

    sim.do_step(t, dt);
    t += dt;
    const auto value3 = sim.get_real(xVar);
    BOOST_TEST((0.0 < value3 && value3 < value2));
    const auto state3 = state2;
    sim.save_state(state3); // Overwrite the previously-saved state

    const auto state1New = sim.import_state(exportedState1);
    sim.restore_state(state1New);
    const auto value1Test = sim.get_real(xVar);
    BOOST_TEST(value1Test == value1);

    sim.restore_state(state3);
    const auto value3Test = sim.get_real(xVar);
    BOOST_TEST(value3Test == value3);

    sim.restore_state(state0);
    t = cosim::time_point();
    sim.start_simulation();
    sim.do_step(t, dt);
    t += dt;
    sim.do_step(t, dt);
    t += dt;
    const auto value0To2Test = sim.get_real(xVar);
    BOOST_TEST(value0To2Test == value2);

    sim.release_state(state0);
    sim.release_state(state1New);
    sim.release_state(state3);
}
