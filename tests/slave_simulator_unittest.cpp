#define BOOST_TEST_MODULE cosim::slave_simulator unittest
#include "mock_slave.hpp"

#include <cosim/algorithm/ecco_algorithm.hpp>
#include <cosim/algorithm/fixed_step_algorithm.hpp>
#include <cosim/error.hpp>
#include <cosim/exception.hpp>
#include <cosim/fmi/fmu.hpp>
#include <cosim/fmi/importer.hpp>
#include <cosim/fs_portability.hpp>
#include <cosim/manipulator/override_manipulator.hpp>
#include <cosim/manipulator/scenario_manager.hpp>
#include <cosim/observer/file_observer.hpp>
#include <cosim/observer/time_series_observer.hpp>
#include <cosim/slave_simulator.hpp>
#include <cosim/utility/filesystem.hpp>

#include <boost/test/unit_test.hpp>

#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <unordered_map>


namespace
{
class exact_value_mock_slave final : public mock_slave
{
public:
    static constexpr cosim::value_reference uint64_in = 10;
    static constexpr cosim::value_reference uint64_out = 11;
    static constexpr cosim::value_reference binary_out = 12;
    static constexpr cosim::value_reference enumeration_out = 13;
    static constexpr cosim::value_reference string_array_out = 14;
    static constexpr cosim::value_reference float32_out = 15;
    static constexpr cosim::value_reference enumeration_in = 16;

    explicit exact_value_mock_slave(bool returnWrongShape = false)
        : returnWrongShape_(returnWrongShape)
    {
        values_.emplace(
            key(cosim::variable_type::uint64, uint64_in),
            cosim::variable_value(cosim::uint64_values{{0, 0}}, {2}));
        values_.emplace(
            key(cosim::variable_type::uint64, uint64_out),
            cosim::variable_value(cosim::uint64_values{{0, 0}}, {2}));
        values_.emplace(
            key(cosim::variable_type::binary, binary_out),
            cosim::variable_value(cosim::binary_values{{cosim::binary_value{{0x00, 0x7f, 0xff}}}}));
        values_.emplace(
            key(cosim::variable_type::enumeration, enumeration_out),
            cosim::variable_value(cosim::enumeration_values{{cosim::enumeration_value{
                (std::numeric_limits<std::int64_t>::min)()}}}));
        values_.emplace(
            key(cosim::variable_type::enumeration, enumeration_in),
            cosim::variable_value(cosim::enumeration_values{{cosim::enumeration_value{1}}}));
        values_.emplace(
            key(cosim::variable_type::string, string_array_out),
            cosim::variable_value(
                cosim::string_values{{"first", "second"}}, {2}));
        values_.emplace(
            key(cosim::variable_type::float32, float32_out),
            cosim::variable_value(cosim::float32_values{{1.25F}}));
    }

    cosim::model_description model_description() const override
    {
        auto description = mock_slave::model_description();
        description.variables.push_back(variable(
            "uint64In",
            uint64_in,
            cosim::variable_type::uint64,
            cosim::variable_causality::input,
            {2},
            values_.at(key(cosim::variable_type::uint64, uint64_in))));
        description.variables.push_back(variable(
            "uint64Out",
            uint64_out,
            cosim::variable_type::uint64,
            cosim::variable_causality::output,
            {2}));
        description.variables.push_back(variable(
            "binaryOut",
            binary_out,
            cosim::variable_type::binary,
            cosim::variable_causality::output));
        description.variables.push_back(variable(
            "enumerationOut",
            enumeration_out,
            cosim::variable_type::enumeration,
            cosim::variable_causality::output));
        description.variables.push_back(variable(
            "enumerationIn",
            enumeration_in,
            cosim::variable_type::enumeration,
            cosim::variable_causality::input));
        description.variables.push_back(variable(
            "stringArrayOut",
            string_array_out,
            cosim::variable_type::string,
            cosim::variable_causality::output,
            {2}));
        description.variables.push_back(variable(
            "float32Out",
            float32_out,
            cosim::variable_type::float32,
            cosim::variable_causality::output));
        return description;
    }

    std::vector<cosim::variable_value> get_variable_values(
        gsl::span<const cosim::typed_value_reference> variables) const override
    {
        std::vector<cosim::variable_value> result;
        result.reserve(variables.size());
        for (const auto variable : variables) {
            if (returnWrongShape_ &&
                variable.type == cosim::variable_type::uint64 &&
                variable.reference == uint64_out) {
                result.emplace_back(cosim::uint64_values{{0}}, std::vector<std::uint64_t>{1});
                continue;
            }
            result.push_back(values_.at(key(variable.type, variable.reference)));
        }
        return result;
    }

    void set_variable_values(
        gsl::span<const cosim::typed_value_reference> variables,
        gsl::span<const cosim::variable_value> values) override
    {
        BOOST_REQUIRE_EQUAL(variables.size(), values.size());
        for (std::size_t i = 0; i < variables.size(); ++i) {
            auto& target =
                values_.at(key(variables[i].type, variables[i].reference));
            BOOST_REQUIRE(target.type() == values[i].type());
            BOOST_REQUIRE(target.dimensions() == values[i].dimensions());
            target = values[i];
            ++assignments;
        }
    }

    cosim::step_result do_step(
        cosim::time_point currentTime,
        cosim::duration stepSize) override
    {
        values_.at(key(cosim::variable_type::uint64, uint64_out)) =
            values_.at(key(cosim::variable_type::uint64, uint64_in));
        return mock_slave::do_step(currentTime, stepSize);
    }

    state_index save_state() override
    {
        const auto state = mock_slave::save_state();
        BOOST_REQUIRE_EQUAL(state, exactStates_.size());
        exactStates_.push_back(values_);
        return state;
    }

    void save_state(state_index state) override
    {
        mock_slave::save_state(state);
        exactStates_.at(state) = values_;
    }

    void restore_state(state_index state) override
    {
        mock_slave::restore_state(state);
        values_ = exactStates_.at(state);
    }

    cosim::serialization::node export_state(state_index state) const override
    {
        auto exported = mock_slave::export_state(state);
        exported.put("exact_state", state);
        return exported;
    }

    state_index import_state(
        const cosim::serialization::node& exported) override
    {
        ++stateImports;
        const auto imported = mock_slave::import_state(exported);
        BOOST_REQUIRE_EQUAL(imported, exactStates_.size());
        exactStates_.push_back(
            exactStates_.at(exported.get<state_index>("exact_state")));
        return imported;
    }

    int assignments = 0;
    int stateImports = 0;

private:
    using value_map = std::unordered_map<std::uint64_t, cosim::variable_value>;

    static std::uint64_t key(
        cosim::variable_type type,
        cosim::value_reference reference)
    {
        return (static_cast<std::uint64_t>(type) << 32U) | reference;
    }

    static cosim::variable_description variable(
        std::string name,
        cosim::value_reference reference,
        cosim::variable_type type,
        cosim::variable_causality causality,
        std::vector<std::uint64_t> dimensions = {},
        std::optional<cosim::variable_value> start = std::nullopt)
    {
        cosim::variable_description description;
        description.name = std::move(name);
        description.reference = reference;
        description.type = type;
        description.causality = causality;
        description.variability = cosim::variable_variability::discrete;
        for (const auto dimension : dimensions) {
            description.dimensions.emplace_back(
                cosim::fixed_dimension{dimension});
        }
        description.array_start = std::move(start);
        return description;
    }

    value_map values_;
    std::vector<value_map> exactStates_;
    bool returnWrongShape_;
};

class enumeration_mock_slave final : public mock_slave
{
public:
    cosim::model_description model_description() const override
    {
        auto model = mock_slave::model_description();
        cosim::variable_type_definition definition;
        definition.name = "Option";
        definition.type = cosim::variable_type::enumeration;
        definition.enumeration_items = {{"one", 1, {}}, {"two", 2, {}}};
        model.type_definitions.push_back(std::move(definition));
        cosim::variable_description variable{};
        variable.name = "enumerationIn";
        variable.reference = 100;
        variable.type = cosim::variable_type::enumeration;
        variable.causality = cosim::variable_causality::input;
        variable.variability = cosim::variable_variability::discrete;
        variable.declared_type = "Option";
        variable.start = cosim::enumeration_value{1};
        model.variables.push_back(variable);
        variable.name = "enumerationOut";
        variable.reference = 101;
        variable.causality = cosim::variable_causality::output;
        variable.start = std::nullopt;
        model.variables.push_back(variable);
        return model;
    }

    std::vector<cosim::variable_value> get_variable_values(
        gsl::span<const cosim::typed_value_reference> variables) const override
    {
        std::vector<cosim::variable_value> result;
        for (const auto variable : variables) {
            if (variable.type != cosim::variable_type::enumeration ||
                (variable.reference != 100 && variable.reference != 101)) {
                throw std::invalid_argument("Unknown mock Enumeration reference");
            }
            result.emplace_back(cosim::enumeration_values{{value}});
        }
        return result;
    }

    void set_variable_values(
        gsl::span<const cosim::typed_value_reference> variables,
        gsl::span<const cosim::variable_value> values) override
    {
        if (variables.size() != values.size()) {
            throw std::invalid_argument("Count mismatch");
        }
        for (std::size_t i = 0; i < variables.size(); ++i) {
            if (variables[i].type != cosim::variable_type::enumeration ||
                variables[i].reference != 100 ||
                values[i].type() != cosim::variable_type::enumeration ||
                !values[i].is_scalar()) {
                throw std::invalid_argument("Invalid mock Enumeration assignment");
            }
            value = std::get<cosim::enumeration_values>(values[i].values()).values[0];
        }
    }

    cosim::enumeration_value value{1};
};

class enumeration_identity_function final : public cosim::function
{
public:
    cosim::function_description description() const override
    {
        cosim::function_description result;
        result.io_groups = {
            {"in", 1, {{"", cosim::variable_type::enumeration, cosim::variable_causality::input, 1, {}}}},
            {"out", 1, {{"", cosim::variable_type::enumeration, cosim::variable_causality::output, 1, {}}}}};
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

#define COSIM_TEST_FUNCTION_CONNECTIONS
#include "connection_type_tests.hpp"
#include "default_slave_batch_tests.hpp"
#undef COSIM_TEST_FUNCTION_CONNECTIONS
#include "modifier_reporting_tests.hpp"

namespace
{
class clocked_reporting_mock_slave final : public mock_slave
{
public:
    cosim::model_description model_description() const override
    {
        auto model = mock_slave::model_description();
        cosim::variable_description variable{};
        variable.name = "clockedArray";
        variable.reference = 20;
        variable.type = cosim::variable_type::uint64;
        variable.causality = cosim::variable_causality::input;
        variable.variability = cosim::variable_variability::discrete;
        variable.dimensions.emplace_back(cosim::fixed_dimension{2});
        variable.array_start = cosim::variable_value(cosim::uint64_values{{1, 2}}, {2});
        variable.clocks = {99};
        model.variables.push_back(std::move(variable));
        return model;
    }
};
}

// Whole-array and clock-associated caches must report active modifiers just
// like scalar caches, including when only one modifier direction remains.
BOOST_AUTO_TEST_CASE(modified_whole_and_clocked_variable_reporting)
{
    cosim::execution execution(
        cosim::time_point(),
        std::make_unique<cosim::fixed_step_algorithm>(cosim::to_duration(0.1)));
    auto manipulator = std::make_shared<reporting_manipulator>();
    execution.add_manipulator(manipulator);
    const auto whole = execution.add_slave(std::make_shared<exact_value_mock_slave>(), "whole");
    const auto clocked = execution.add_slave(std::make_shared<clocked_reporting_mock_slave>(), "clocked");
    const auto identity = [](const cosim::variable_value& value, cosim::duration) { return value; };
    const auto type = cosim::variable_type::uint64;
    const auto check = [&execution](cosim::simulator_index simulator, cosim::value_reference reference) {
        const auto modified = execution.get_modified_variables();
        return std::count_if(modified.begin(), modified.end(), [=](const auto& variable) {
            return variable.simulator == simulator &&
                variable.type == cosim::variable_type::uint64 && variable.reference == reference;
        });
    };
    auto* a = manipulator->simulators.at(whole);
    auto* b = manipulator->simulators.at(clocked);
    const auto wholeReference = exact_value_mock_slave::uint64_in;
    a->expose_for_setting(type, wholeReference);
    a->expose_for_getting(type, wholeReference);
    a->set_variable_input_modifier(type, wholeReference, identity);
    a->set_variable_output_modifier(type, wholeReference, identity);
    b->expose_for_setting(type, 20);
    b->expose_for_getting(type, 20);
    b->set_variable_input_modifier(type, 20, identity);
    b->set_variable_output_modifier(type, 20, identity);
    BOOST_TEST(check(whole, wholeReference) == 1);
    BOOST_TEST(check(clocked, 20) == 1);
    a->set_variable_input_modifier(type, wholeReference, {});
    b->set_variable_output_modifier(type, 20, {});
    BOOST_TEST(check(whole, wholeReference) == 1);
    BOOST_TEST(check(clocked, 20) == 1);
    a->set_variable_output_modifier(type, wholeReference, {});
    b->set_variable_input_modifier(type, 20, {});
    BOOST_TEST(execution.get_modified_variables().empty());
}

BOOST_AUTO_TEST_CASE(output_modifiers_require_exposure)
{
    cosim::slave_simulator simulator(std::make_shared<mock_slave>(), "modifier");
    const auto realModifier = [](double value, cosim::duration) { return value + 1.0; };
    const auto integerModifier = [](int value, cosim::duration) { return value + 1; };
    const auto booleanModifier = [](bool value, cosim::duration) { return !value; };
    const auto stringModifier = [](std::string_view value, cosim::duration) { return std::string(value) + "!"; };
    const auto unexposed = mock_slave::real_in_reference;
    const auto exposed = mock_slave::real_out_reference;

    BOOST_CHECK_THROW(simulator.set_real_output_modifier(unexposed, realModifier), std::out_of_range);
    BOOST_CHECK_THROW(simulator.set_integer_output_modifier(unexposed, integerModifier), std::out_of_range);
    BOOST_CHECK_THROW(simulator.set_boolean_output_modifier(unexposed, booleanModifier), std::out_of_range);
    BOOST_CHECK_THROW(simulator.set_string_output_modifier(unexposed, stringModifier), std::out_of_range);

    simulator.expose_for_getting(cosim::variable_type::real, exposed);
    simulator.expose_for_getting(cosim::variable_type::integer, exposed);
    simulator.expose_for_getting(cosim::variable_type::boolean, exposed);
    simulator.expose_for_getting(cosim::variable_type::string, exposed);
    simulator.set_real_output_modifier(exposed, realModifier);
    simulator.set_integer_output_modifier(exposed, integerModifier);
    simulator.set_boolean_output_modifier(exposed, booleanModifier);
    simulator.set_string_output_modifier(exposed, stringModifier);
    BOOST_CHECK_EXCEPTION(
        simulator.set_real_output_modifier(unexposed, {}),
        std::out_of_range,
        [](const std::out_of_range& error) {
            return std::string(error.what()).find("exposed") != std::string::npos;
        });
    BOOST_CHECK_THROW(simulator.set_integer_output_modifier(unexposed, {}), std::out_of_range);
    BOOST_CHECK_THROW(simulator.set_boolean_output_modifier(unexposed, {}), std::out_of_range);
    BOOST_CHECK_THROW(simulator.set_string_output_modifier(unexposed, {}), std::out_of_range);
    simulator.setup(cosim::time_point(), {}, {});
    simulator.start_simulation();
    simulator.do_step(cosim::time_point(), cosim::to_duration(1.0));
    BOOST_TEST(simulator.get_real(exposed) == 1.0);
    BOOST_TEST(simulator.get_integer(exposed) == 1);
    BOOST_TEST(simulator.get_boolean(exposed));
    BOOST_TEST(simulator.get_string(exposed) == "!");

    simulator.set_real_output_modifier(exposed, {});
    simulator.set_integer_output_modifier(exposed, {});
    simulator.set_boolean_output_modifier(exposed, {});
    simulator.set_string_output_modifier(exposed, {});
    simulator.do_step(cosim::to_time_point(1.0), cosim::to_duration(1.0));
    BOOST_TEST(simulator.get_real(exposed) == 0.0);
    BOOST_TEST(simulator.get_integer(exposed) == 0);
    BOOST_TEST(!simulator.get_boolean(exposed));
    BOOST_TEST(simulator.get_string(exposed).empty());
}

BOOST_AUTO_TEST_CASE(legacy_enumeration_cache_import)
{
    auto slave = std::make_shared<exact_value_mock_slave>();
    cosim::slave_simulator simulator(slave, "legacy-enumeration");
    const auto type = cosim::variable_type::enumeration;
    const auto input = exact_value_mock_slave::enumeration_in;
    const auto output = exact_value_mock_slave::enumeration_out;
    simulator.expose_for_setting(type, input);
    simulator.expose_for_getting(type, output);
    simulator.setup(cosim::time_point(), {}, {});
    simulator.set_variable_value(type, input, cosim::variable_value(cosim::integer_values{{2}}));
    const auto saved = simulator.save_state();
    auto legacy = simulator.export_state(saved);
    BOOST_TEST(legacy.get<int>("scheme_version") == 4);
    legacy.put("scheme_version", 0);
    legacy.erase("whole_get_cache");
    legacy.erase("whole_set_cache");
    legacy.erase("clocked_get_cache");
    legacy.erase("clocked_set_cache");
    cosim::serialization::node getCache;
    cosim::serialization::node getEntry;
    getEntry.put("has_value", true);
    getEntry.put("value", (std::numeric_limits<std::int64_t>::min)());
    getCache.put_child(std::to_string(output), getEntry);
    legacy.put_child("enumeration_get_cache", getCache);
    cosim::serialization::node setCache;
    cosim::serialization::node setEntry;
    setEntry.put("has_value", true);
    setEntry.put("value", std::int64_t{2});
    setEntry.put("pending", true);
    setCache.put_child(std::to_string(input), setEntry);
    legacy.put_child("enumeration_set_cache", setCache);

    std::stringstream serialized(std::ios::in | std::ios::out | std::ios::binary);
    serialized << cosim::serialization::format::cbor << legacy;
    serialized.seekg(0);
    cosim::serialization::node decoded;
    serialized >> cosim::serialization::format::cbor >> decoded;
    const auto imported = simulator.import_state(decoded);
    simulator.restore_state(imported);
    const auto upgraded = simulator.save_state();
    const auto upgradedExport = simulator.export_state(upgraded);
    BOOST_TEST(upgradedExport.get<int>("scheme_version") == 4);
    const auto reimported = simulator.import_state(upgradedExport);
    simulator.restore_state(reimported);
    BOOST_TEST(std::get<cosim::enumeration_values>(
        simulator.get_variable_value(type, output).values()).values[0].value ==
        (std::numeric_limits<std::int64_t>::min)());
    const auto assignments = slave->assignments;
    simulator.do_iteration();
    const cosim::typed_value_reference inputReference{type, input};
    auto values = slave->get_variable_values(gsl::make_span(&inputReference, 1));
    BOOST_TEST(std::get<cosim::enumeration_values>(values[0].values()).values[0].value == 2);
    BOOST_TEST(slave->assignments == assignments + 1);
    simulator.do_iteration();
    BOOST_TEST(slave->assignments == assignments + 1);

    const auto checkCorrupt = [&](const cosim::serialization::node& node) {
        const auto imports = slave->stateImports;
        BOOST_CHECK_EXCEPTION(simulator.import_state(node), cosim::error,
            [](const cosim::error& error) {
                return error.code() == cosim::make_error_code(cosim::errc::bad_file);
            });
        BOOST_TEST(slave->stateImports == imports);
    };
    auto malformed = legacy;
    malformed.erase("enumeration_get_cache");
    checkCorrupt(malformed);
    malformed = legacy;
    malformed.put("enumeration_set_cache." + std::to_string(input) + ".has_value", false);
    checkCorrupt(malformed);
    malformed = legacy;
    malformed.put("enumeration_set_cache." + std::to_string(input) + ".value", std::string("not-an-integer"));
    checkCorrupt(malformed);
    malformed = legacy;
    malformed.get_child("enumeration_get_cache").add_child("13", getEntry);
    checkCorrupt(malformed);
    malformed = legacy;
    malformed.add_child("enumeration_set_cache", setCache);
    checkCorrupt(malformed);
    malformed = legacy;
    malformed.get_child("enumeration_set_cache").data() = std::int32_t{1};
    checkCorrupt(malformed);
    malformed = legacy;
    malformed.get_child("enumeration_get_cache").add_child("4294967296", getEntry);
    checkCorrupt(malformed);
    malformed = legacy;
    malformed.get_child("enumeration_get_cache").add_child("13suffix", getEntry);
    checkCorrupt(malformed);
    malformed = legacy;
    malformed.get_child("enumeration_get_cache").add_child("999", getEntry);
    checkCorrupt(malformed);

    auto noCaches = legacy;
    noCaches.erase("enumeration_get_cache");
    noCaches.erase("enumeration_set_cache");
    const auto preEnumeration = simulator.import_state(noCaches);
    simulator.restore_state(preEnumeration);
    BOOST_CHECK_THROW(static_cast<void>(simulator.get_variable_value(type, output)), std::logic_error);
    simulator.do_iteration();
    values = slave->get_variable_values(gsl::make_span(&inputReference, 1));
    BOOST_TEST(std::get<cosim::enumeration_values>(values[0].values()).values[0].value == 1);
    simulator.set_variable_input_modifier(type, input,
        [](const cosim::variable_value& value, cosim::duration) { return value; });
    BOOST_CHECK_THROW(simulator.do_iteration(), cosim::error);
    simulator.set_variable_input_modifier(type, input, {});
    simulator.release_state(preEnumeration);
    simulator.release_state(reimported);
    simulator.release_state(upgraded);
    simulator.release_state(imported);
    simulator.release_state(saved);
}

BOOST_AUTO_TEST_CASE(legacy_enumeration_cache_import_fmi2)
{
    const auto referenceFmuDir = std::getenv("REFERENCE_FMU_V2_DIR");
    BOOST_REQUIRE(referenceFmuDir != nullptr);
    auto importer = cosim::fmi::importer::create();
    auto fmu = importer->import(cosim::filesystem::path(referenceFmuDir) / "Feedthrough.fmu");
    const auto description = fmu->model_description();
    const auto input = cosim::find_variable(*description, "Enumeration_input");
    const auto output = cosim::find_variable(*description, "Enumeration_output");
    BOOST_REQUIRE(input);
    BOOST_REQUIRE(output);
    const auto type = cosim::variable_type::enumeration;
    cosim::slave_simulator simulator(fmu->instantiate_slave("legacy-state"), "legacy-state");
    simulator.expose_for_setting(type, input->reference);
    simulator.expose_for_getting(type, output->reference);
    simulator.setup(cosim::time_point(), {}, {});
    simulator.start_simulation();
    simulator.set_variable_value(type, input->reference, cosim::variable_value(cosim::integer_values{{2}}));
    const auto saved = simulator.save_state();
    auto legacy = simulator.export_state(saved);
    legacy.put("scheme_version", 0);
    legacy.erase("whole_get_cache");
    legacy.erase("whole_set_cache");
    legacy.erase("clocked_get_cache");
    legacy.erase("clocked_set_cache");
    cosim::serialization::node getCache;
    cosim::serialization::node getEntry;
    getEntry.put("has_value", true);
    getEntry.put("value", std::int64_t{1});
    getCache.put_child(std::to_string(output->reference), getEntry);
    legacy.put_child("enumeration_get_cache", getCache);
    cosim::serialization::node setCache;
    cosim::serialization::node setEntry;
    setEntry.put("has_value", true);
    setEntry.put("value", std::int64_t{2});
    setEntry.put("pending", true);
    setCache.put_child(std::to_string(input->reference), setEntry);
    legacy.put_child("enumeration_set_cache", setCache);
    std::stringstream serialized(std::ios::in | std::ios::out | std::ios::binary);
    serialized << cosim::serialization::format::cbor << legacy;
    serialized.seekg(0);
    cosim::serialization::node decoded;
    serialized >> cosim::serialization::format::cbor >> decoded;

    const auto imported = simulator.import_state(decoded);
    simulator.restore_state(imported);
    BOOST_TEST(std::get<cosim::enumeration_values>(
        simulator.get_variable_value(type, output->reference).values()).values[0].value == 1);
    simulator.do_step(cosim::time_point(), cosim::to_duration(0.1));
    BOOST_TEST(std::get<cosim::enumeration_values>(
        simulator.get_variable_value(type, output->reference).values()).values[0].value == 2);
    simulator.release_state(imported);
    simulator.release_state(saved);
}

BOOST_AUTO_TEST_CASE(enumeration_function_and_algorithm_connections)
{
    const auto testConnections = [](std::shared_ptr<cosim::algorithm> algorithm) {
        auto source = std::make_shared<enumeration_mock_slave>();
        auto direct = std::make_shared<enumeration_mock_slave>();
        auto throughFunction = std::make_shared<enumeration_mock_slave>();
        cosim::execution execution(cosim::time_point(), std::move(algorithm));
        const auto sourceId = execution.add_slave(source, "source");
        const auto directId = execution.add_slave(direct, "direct");
        const auto targetId = execution.add_slave(throughFunction, "function-target");
        const auto functionId = execution.add_function(std::make_shared<enumeration_identity_function>());
        const cosim::variable_id sourceOutput{sourceId, cosim::variable_type::enumeration, 101};
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

BOOST_AUTO_TEST_CASE(enumeration_time_series_and_file_observation)
{
    cosim::utility::temp_dir directory;
    cosim::file_observer_config config;
    BOOST_CHECK_THROW(config.log_simulator_variables("enumeration", {}, 0), std::invalid_argument);
    BOOST_CHECK_THROW(config.log_all_simulator_variables("enumeration", 0), std::invalid_argument);
    config.set_timestamped_filenames(false)
        .log_simulator_variables("enumeration", {"enumerationIn", "enumerationOut"});
    cosim::execution execution(
        cosim::time_point(),
        std::make_shared<cosim::fixed_step_algorithm>(cosim::to_duration(0.1)));
    auto series = std::make_shared<cosim::time_series_observer>();
    auto file = std::make_shared<cosim::file_observer>(directory.path(), config);
    execution.add_observer(series);
    execution.add_observer(file);
    const auto sim = execution.add_slave(std::make_shared<enumeration_mock_slave>(), "enumeration");
    series->start_observing({sim, cosim::variable_type::enumeration, 101});
    cosim::step_number initialSteps[2];
    series->get_step_numbers(sim, cosim::duration::zero(), initialSteps);
    BOOST_TEST(initialSteps[0] == 0);
    BOOST_TEST(initialSteps[1] == 0);
    BOOST_REQUIRE(execution.simulate_until(cosim::to_time_point(0.2)));
    cosim::variable_value values[4];
    cosim::step_number steps[4];
    cosim::time_point times[4];
    const auto count = series->get_variable_samples(
        sim, cosim::variable_type::enumeration, 101, 0, values, steps, times);
    BOOST_REQUIRE_GT(count, 0U);
    cosim::step_number tooShort[1];
    BOOST_CHECK_THROW(
        series->get_step_numbers(sim, cosim::duration::zero(), tooShort),
        std::invalid_argument);
    BOOST_TEST(std::get<cosim::enumeration_values>(values[0].values()).values[0].value == 1);
    series->stop_observing({sim, cosim::variable_type::enumeration, 101});
    file->stop_recording();

    std::ifstream csv(directory.path() / "enumeration.csv");
    const std::string contents((std::istreambuf_iterator<char>(csv)), std::istreambuf_iterator<char>());
    BOOST_TEST(contents.find("enumerationOut") != std::string::npos);
    BOOST_TEST(contents.find(",1,1") != std::string::npos);
    std::ifstream metadata(directory.path() / "enumeration_metadata.yaml");
    const std::string yaml((std::istreambuf_iterator<char>(metadata)), std::istreambuf_iterator<char>());
    BOOST_TEST(yaml.find("enumerationIn") != std::string::npos);
    BOOST_TEST(yaml.find("start value:  1") != std::string::npos);
}

BOOST_AUTO_TEST_CASE(execution_lifecycle_errors_are_reported)
{
    cosim::execution execution(
        cosim::time_point(),
        std::make_shared<cosim::fixed_step_algorithm>(cosim::to_duration(0.1)));
    BOOST_CHECK(!execution.is_initialized());
    BOOST_CHECK_THROW(execution.export_current_state(), std::logic_error);
    BOOST_CHECK_THROW(execution.import_state(cosim::serialization::node{}), std::logic_error);
    const auto slave = std::make_shared<mock_slave>();
    const auto index = execution.add_slave(slave, "initial");
    BOOST_CHECK(execution.get_slave(index) == slave);
    BOOST_CHECK_THROW(execution.get_slave(-1), std::out_of_range);
    BOOST_CHECK_THROW(execution.get_slave(index + 1), std::out_of_range);
    execution.initialize();
    BOOST_CHECK(execution.is_initialized());
    BOOST_CHECK(execution.get_slave(index) == slave);
    BOOST_CHECK_THROW(execution.add_slave(std::make_shared<mock_slave>(), "late"), std::logic_error);
    BOOST_CHECK_THROW(execution.add_function({}), std::logic_error);
}

BOOST_AUTO_TEST_CASE(algorithm_basic_configuration_is_observable)
{
    const auto step = cosim::to_duration(0.1);
    cosim::fixed_step_algorithm fixed(step, 0);
    BOOST_CHECK(fixed.base_step_size() == step);
    BOOST_TEST(fixed.worker_thread_count() == 0U);
    fixed.set_max_event_iterations(27);
    BOOST_TEST(fixed.max_event_iterations() == 27U);
    cosim::ecco_algorithm adaptive(cosim::ecco_algorithm_params{
        0.99, step, cosim::to_duration(0.01), step,
        0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15}, 0);
    BOOST_CHECK(adaptive.initial_step_size() == step);
    BOOST_TEST(adaptive.worker_thread_count() == 0U);
    adaptive.set_max_event_iterations(31);
    BOOST_TEST(adaptive.max_event_iterations() == 31U);
}

BOOST_AUTO_TEST_CASE(enumeration_scenario_and_override)
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

BOOST_AUTO_TEST_CASE(enumeration_without_start_is_not_fabricated)
{
    auto slave = std::make_shared<exact_value_mock_slave>();
    cosim::slave_simulator simulator(slave, "enumeration-no-start");
    const auto reference = exact_value_mock_slave::enumeration_in;
    const cosim::typed_value_reference variable{
        cosim::variable_type::enumeration, reference};
    simulator.expose_for_setting(variable.type, reference);
    simulator.setup(cosim::time_point(), {}, {});
    simulator.start_simulation();
    simulator.do_iteration();
    auto values = slave->get_variable_values(gsl::make_span(&variable, 1));
    BOOST_TEST(std::get<cosim::enumeration_values>(values[0].values())
        .values[0].value == 1);
    const auto saved = simulator.save_state();
    const auto exported = simulator.export_state(saved);
    const auto imported = simulator.import_state(exported);
    simulator.restore_state(imported);
    simulator.set_variable_input_modifier(
        variable.type, reference,
        [](const cosim::variable_value& value, cosim::duration) { return value; });
    BOOST_CHECK_THROW(simulator.do_iteration(), cosim::error);
    simulator.set_variable_input_modifier(variable.type, reference, {});
    simulator.set_variable_value(
        variable.type, reference, cosim::variable_value(cosim::integer_values{{2}}));
    simulator.do_iteration();
    values = slave->get_variable_values(gsl::make_span(&variable, 1));
    BOOST_TEST(values[0].type() == cosim::variable_type::enumeration);
    BOOST_TEST(std::get<cosim::enumeration_values>(values[0].values())
        .values[0].value == 2);
    simulator.set_variable_input_modifier(
        variable.type, reference,
        [](const cosim::variable_value&, cosim::duration) {
            return cosim::variable_value(cosim::integer_values{{1}});
        });
    simulator.do_iteration();
    values = slave->get_variable_values(gsl::make_span(&variable, 1));
    BOOST_TEST(values[0].type() == cosim::variable_type::enumeration);
    BOOST_TEST(std::get<cosim::enumeration_values>(values[0].values())
        .values[0].value == 1);
    simulator.set_variable_input_modifier(variable.type, reference, {});

    const auto output = exact_value_mock_slave::enumeration_out;
    simulator.expose_for_getting(variable.type, output);
    simulator.set_variable_output_modifier(
        variable.type, output,
        [](const cosim::variable_value&, cosim::duration) {
            return cosim::variable_value(cosim::integer_values{{2}});
        });
    simulator.do_iteration();
    const auto modified = simulator.get_variable_value(variable.type, output);
    BOOST_TEST(modified.type() == cosim::variable_type::enumeration);
    BOOST_TEST(std::get<cosim::enumeration_values>(modified.values())
        .values[0].value == 2);
    BOOST_CHECK_THROW(simulator.save_state(), cosim::error);
    simulator.set_variable_output_modifier(
        variable.type, output,
        [](const cosim::variable_value&, cosim::duration) {
            return cosim::variable_value(cosim::integer_values{{1, 2}}, {2});
        });
    BOOST_CHECK_THROW(simulator.do_iteration(), cosim::error);
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
}


BOOST_AUTO_TEST_CASE(slave_default_whole_value_rejects_unsupported_types)
{
    mock_slave slave;
    const std::vector<cosim::variable_value> unsupportedValues{
        cosim::variable_value(cosim::enumeration_values{{cosim::enumeration_value{1}}}),
        cosim::variable_value(cosim::float32_values{{1.0F}}),
        cosim::variable_value(cosim::int8_values{{1}}),
        cosim::variable_value(cosim::uint8_values{{1}}),
        cosim::variable_value(cosim::int16_values{{1}}),
        cosim::variable_value(cosim::uint16_values{{1}}),
        cosim::variable_value(cosim::uint32_values{{1}}),
        cosim::variable_value(cosim::int64_values{{1}}),
        cosim::variable_value(cosim::uint64_values{{1}}),
        cosim::variable_value(cosim::binary_values{{cosim::binary_value{{1}}}}),
    };
    const auto unsupported = [](const cosim::error& error) {
        return error.code() ==
            cosim::make_error_code(cosim::errc::unsupported_feature);
    };
    for (const auto& value : unsupportedValues) {
        const cosim::typed_value_reference variable{value.type(), 0};
        BOOST_CHECK_EXCEPTION(
            slave.get_variable_values(gsl::make_span(&variable, 1)),
            cosim::error,
            unsupported);
        BOOST_CHECK_EXCEPTION(
            slave.set_variable_values(
                gsl::make_span(&variable, 1), gsl::make_span(&value, 1)),
            cosim::error,
            unsupported);
    }

    for (const auto type : {cosim::variable_type::clock,
             static_cast<cosim::variable_type>(-1)}) {
        const cosim::typed_value_reference variable{type, 0};
        BOOST_CHECK_EXCEPTION(
            slave.get_variable_values(gsl::make_span(&variable, 1)),
            cosim::error,
            unsupported);
    }

    const cosim::typed_value_reference mixed[] = {
        {cosim::variable_type::real, mock_slave::real_in_reference},
        {cosim::variable_type::float32, 0}};
    BOOST_CHECK_EXCEPTION(
        slave.get_variable_values(mixed), cosim::error, unsupported);
}


BOOST_AUTO_TEST_CASE(slave_simulator_whole_value_transport)
{
    const auto slave = std::make_shared<exact_value_mock_slave>();
    cosim::slave_simulator simulator(slave, "exact");
    const auto exposeOutput = [&simulator](
                                  cosim::variable_type type,
                                  cosim::value_reference reference) {
        simulator.expose_for_getting(type, reference);
    };
    exposeOutput(cosim::variable_type::uint64, exact_value_mock_slave::uint64_out);
    exposeOutput(cosim::variable_type::binary, exact_value_mock_slave::binary_out);
    exposeOutput(
        cosim::variable_type::enumeration,
        exact_value_mock_slave::enumeration_out);
    exposeOutput(cosim::variable_type::string, exact_value_mock_slave::string_array_out);
    exposeOutput(cosim::variable_type::float32, exact_value_mock_slave::float32_out);
    exposeOutput(cosim::variable_type::real, mock_slave::real_out_reference);
    simulator.expose_for_setting(
        cosim::variable_type::uint64, exact_value_mock_slave::uint64_in);
    simulator.expose_for_setting(
        cosim::variable_type::real, mock_slave::real_in_reference);

    const cosim::variable_value maximumValues(
        cosim::uint64_values{{(std::numeric_limits<std::uint64_t>::max)(), 42}},
        {2});
    BOOST_CHECK_THROW(
        simulator.set_variable_value(
            cosim::variable_type::uint64,
            exact_value_mock_slave::uint64_in,
            cosim::variable_value(cosim::uint64_values{{1}}, {1})),
        std::invalid_argument);

    simulator.set_variable_input_modifier(
        cosim::variable_type::uint64,
        exact_value_mock_slave::uint64_in,
        [maximumValues](const cosim::variable_value&, cosim::duration) {
            return maximumValues;
        });
    simulator.set_variable_value(
        cosim::variable_type::real,
        mock_slave::real_in_reference,
        cosim::variable_value(cosim::real_values{{2.0}}));
    simulator.set_variable_input_modifier(
        cosim::variable_type::real,
        mock_slave::real_in_reference,
        [](const cosim::variable_value& value, cosim::duration) {
            const auto input = std::get<cosim::real_values>(value.values()).values[0];
            return cosim::variable_value(cosim::real_values{{input + 1.0}});
        });
    simulator.setup(cosim::time_point(), {}, {});
    simulator.start_simulation();
    simulator.do_step(cosim::time_point(), cosim::to_duration(1.0));
    BOOST_CHECK(
        simulator.get_variable_value(
            cosim::variable_type::uint64,
            exact_value_mock_slave::uint64_out) == maximumValues);
    BOOST_CHECK(
        simulator.get_variable_value(
            cosim::variable_type::real,
            mock_slave::real_out_reference) ==
        cosim::variable_value(cosim::real_values{{3.0}}));

    simulator.set_variable_input_modifier(
        cosim::variable_type::uint64,
        exact_value_mock_slave::uint64_in,
        {});
    simulator.set_variable_input_modifier(
        cosim::variable_type::real,
        mock_slave::real_in_reference,
        {});
    simulator.set_variable_output_modifier(
        cosim::variable_type::float32,
        exact_value_mock_slave::float32_out,
        [](const cosim::variable_value& value, cosim::duration) {
            auto result = std::get<cosim::float32_values>(value.values());
            result.values[0] *= 2.0F;
            return cosim::variable_value(std::move(result));
        });
    simulator.do_iteration();
    const cosim::typed_value_reference arrayInput{
        cosim::variable_type::uint64, exact_value_mock_slave::uint64_in};
    BOOST_CHECK(
        slave->get_variable_values(gsl::make_span(&arrayInput, 1))[0] ==
        cosim::variable_value(cosim::uint64_values{{0, 0}}, {2}));
    BOOST_CHECK(
        simulator.get_variable_value(
            cosim::variable_type::float32,
            exact_value_mock_slave::float32_out) ==
        cosim::variable_value(cosim::float32_values{{2.5F}}));
    BOOST_CHECK_THROW(simulator.save_state(), cosim::error);
    simulator.set_variable_output_modifier(
        cosim::variable_type::float32,
        exact_value_mock_slave::float32_out,
        {});

    const cosim::variable_value pendingValues(
        cosim::uint64_values{{7, 8}}, {2});
    simulator.set_variable_value(
        cosim::variable_type::uint64,
        exact_value_mock_slave::uint64_in,
        pendingValues);
    simulator.set_variable_value(
        cosim::variable_type::real,
        mock_slave::real_in_reference,
        cosim::variable_value(cosim::real_values{{9.0}}));
    const auto saved = simulator.save_state();
    const auto exported = simulator.export_state(saved);
    simulator.set_variable_value(
        cosim::variable_type::uint64,
        exact_value_mock_slave::uint64_in,
        cosim::variable_value(cosim::uint64_values{{1, 2}}, {2}));
    simulator.do_step(
        cosim::to_time_point(1.0), cosim::to_duration(1.0));
    BOOST_CHECK(
        simulator.get_variable_value(
            cosim::variable_type::uint64,
            exact_value_mock_slave::uint64_out) != maximumValues);

    const auto imported = simulator.import_state(exported);
    simulator.restore_state(imported);
    BOOST_CHECK(
        simulator.get_variable_value(
            cosim::variable_type::uint64,
            exact_value_mock_slave::uint64_out) == maximumValues);
    BOOST_CHECK(
        simulator.get_variable_value(
            cosim::variable_type::binary,
            exact_value_mock_slave::binary_out) ==
        cosim::variable_value(cosim::binary_values{{cosim::binary_value{{0x00, 0x7f, 0xff}}}}));
    BOOST_CHECK(
        simulator.get_variable_value(
            cosim::variable_type::enumeration,
            exact_value_mock_slave::enumeration_out) ==
        cosim::variable_value(cosim::enumeration_values{{cosim::enumeration_value{
            (std::numeric_limits<std::int64_t>::min)()}}}));
    BOOST_CHECK(
        simulator.get_variable_value(
            cosim::variable_type::string,
            exact_value_mock_slave::string_array_out) ==
        cosim::variable_value(
            cosim::string_values{{"first", "second"}}, {2}));
    simulator.do_step(
        cosim::to_time_point(1.0), cosim::to_duration(1.0));
    BOOST_CHECK(
        simulator.get_variable_value(
            cosim::variable_type::uint64,
            exact_value_mock_slave::uint64_out) == pendingValues);
    BOOST_CHECK(
        simulator.get_variable_value(
            cosim::variable_type::real,
            mock_slave::real_out_reference) ==
        cosim::variable_value(cosim::real_values{{9.0}}));
}


BOOST_AUTO_TEST_CASE(slave_simulator_rejects_initial_shape_mismatch)
{
    cosim::slave_simulator simulator(
        std::make_shared<exact_value_mock_slave>(true), "wrongShape");
    simulator.expose_for_getting(
        cosim::variable_type::uint64,
        exact_value_mock_slave::uint64_out);
    BOOST_CHECK_THROW(
        simulator.setup(cosim::time_point(), {}, {}),
        cosim::error);
}


BOOST_AUTO_TEST_CASE(fixed_communication_step_is_checked_and_restored)
{
    struct fixed_step_slave : mock_slave
    {
        cosim::model_description model_description() const override
        {
            auto description = mock_slave::model_description();
            description.capabilities.can_handle_variable_step = false;
            return description;
        }
    };
    auto slave = std::make_shared<fixed_step_slave>();
    cosim::slave_simulator simulator(slave, "fixed-step-slave");
    const auto one = cosim::to_duration(1.0);
    const auto half = cosim::to_duration(0.5);
    simulator.setup(cosim::time_point{}, {}, {});
    simulator.start_simulation();
    const auto initial = simulator.save_state();
    BOOST_CHECK(simulator.do_step(cosim::time_point{}, one) == cosim::step_result::complete);
    const auto saved = simulator.save_state();
    const auto exported = simulator.export_state(saved);
    BOOST_CHECK_THROW(simulator.do_step_with_info(cosim::to_time_point(1.0), half), cosim::error);
    const auto imported = simulator.import_state(exported);
    simulator.restore_state(imported);
    BOOST_CHECK_THROW(simulator.do_step_with_info(cosim::to_time_point(1.0), half), cosim::error);
    BOOST_CHECK(simulator.do_step_with_info(cosim::to_time_point(1.0), one).result ==
        cosim::step_result::complete);
    auto old = exported;
    old.put("scheme_version", 3);
    BOOST_CHECK_EXCEPTION(simulator.import_state(old), cosim::error,
        [](const cosim::error& error) {
            return error.code() == cosim::make_error_code(cosim::errc::unsupported_feature);
        });
    simulator.restore_state(initial);
    BOOST_CHECK(simulator.do_step_with_info(cosim::time_point{}, half).result ==
        cosim::step_result::complete);
    simulator.release_state(initial);
    simulator.release_state(saved);
    simulator.release_state(imported);
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
    auto version0State = exportedState1;
    version0State.put("scheme_version", 0);
    const auto importedVersion0 = sim.import_state(version0State);
    sim.release_state(importedVersion0);
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
