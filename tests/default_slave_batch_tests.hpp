#ifndef COSIM_TEST_DEFAULT_SLAVE_BATCH_TESTS_HPP
#define COSIM_TEST_DEFAULT_SLAVE_BATCH_TESTS_HPP

#include "mock_slave.hpp"

#include <cosim/error.hpp>
#include <boost/test/unit_test.hpp>

#include <algorithm>
#include <type_traits>
#include <vector>

namespace
{
class batch_validation_slave : public mock_slave
{
public:
    cosim::model_description model_description() const override
    {
        ++modelRequests;
        auto model = mock_slave::model_description();
        if (omitInteger) {
            model.variables.erase(
                std::remove_if(model.variables.begin(), model.variables.end(),
                    [](const auto& variable) {
                        return variable.type == cosim::variable_type::integer;
                    }),
                model.variables.end());
        }
        for (auto& variable : model.variables) {
            if (variable.type == cosim::variable_type::integer &&
                variable.reference == integer_in_reference && arrayInteger) {
                variable.dimensions.push_back(cosim::fixed_dimension{2});
            }
            if (variable.type == cosim::variable_type::real &&
                variable.reference == real_in_reference && realBounds) {
                variable.min = 1.0;
                variable.max = 2.0;
            }
        }
        if (addAlias) {
            model.variables.push_back(model.variables[1]);
            model.variables.back().name = "realAlias";
        }
        return model;
    }

    void set_real_variables(gsl::span<const cosim::value_reference> references,
        gsl::span<const double> values) override
    {
        ++setterCalls;
        mock_slave::set_real_variables(references, values);
    }
    void set_integer_variables(gsl::span<const cosim::value_reference> references,
        gsl::span<const int> values) override
    {
        ++setterCalls;
        mock_slave::set_integer_variables(references, values);
    }
    void set_boolean_variables(gsl::span<const cosim::value_reference> references,
        gsl::span<const bool> values) override
    {
        ++setterCalls;
        mock_slave::set_boolean_variables(references, values);
    }
    void set_string_variables(gsl::span<const cosim::value_reference> references,
        gsl::span<const std::string> values) override
    {
        ++setterCalls;
        if (failString) throw std::runtime_error("deliberate setter failure");
        mock_slave::set_string_variables(references, values);
    }

    mutable int modelRequests = 0;
    int setterCalls = 0;
    bool omitInteger = false;
    bool arrayInteger = false;
    bool realBounds = false;
    bool addAlias = false;
    bool failString = false;
};
} // namespace

// Every bridge rejection must precede all writes, even when a valid first
// entry would otherwise be dispatched before a bad later entry.
BOOST_AUTO_TEST_CASE(default_slave_batch_prevalidation)
{
    using namespace cosim;
    batch_validation_slave slave;
    const typed_value_reference first{variable_type::real, mock_slave::real_in_reference};
    const variable_value firstValue(real_values{{7.0}});
    const std::vector<variable_value> supported{
        variable_value(real_values{{1.0}}), variable_value(integer_values{{1}}),
        variable_value(boolean_values{{true}}), variable_value(string_values{{"x"}})};
    const auto rejected = [&](const char* reason, typed_value_reference second,
                              const variable_value& badValue, bool inputError) {
        BOOST_TEST_CONTEXT(reason << " for " << to_text(second.type)) {
            const typed_value_reference references[] = {first, second};
            const variable_value values[] = {firstValue, badValue};
            if (inputError) {
                BOOST_CHECK_THROW(slave.set_variable_values(references, values), std::invalid_argument);
            } else {
                BOOST_CHECK_THROW(slave.set_variable_values(references, values), cosim::error);
            }
            BOOST_TEST(slave.setterCalls == 0);
        }
    };

    for (const auto& value : supported) {
        rejected("unknown typed reference", {value.type(), 99999}, value, false);
        const auto wrong = value.type() == variable_type::real
            ? variable_value(integer_values{{1}}) : variable_value(real_values{{1.0}});
        rejected("mismatched value type", {value.type(), 1}, wrong, true);
        const auto array = std::visit([](const auto& source) {
            auto copy = source;
            typename std::decay_t<decltype(copy)>::value_type element = copy.values[0];
            copy.values.push_back(element);
            return variable_value(std::move(copy), {2});
        }, value.values());
        rejected("array value on scalar bridge", {value.type(), 1}, array, false);
    }

    const std::vector<variable_value> unsupported{
        variable_value(enumeration_values{{enumeration_value{1}}}),
        variable_value(float32_values{{1.0F}}), variable_value(int8_values{{1}}),
        variable_value(uint8_values{{1}}), variable_value(int16_values{{1}}),
        variable_value(uint16_values{{1}}), variable_value(uint32_values{{1}}),
        variable_value(int64_values{{1}}), variable_value(uint64_values{{1}}),
        variable_value(binary_values{{binary_value{{1}}}})};
    for (const auto& value : unsupported) {
        rejected("unsupported generic type", {value.type(), 1}, value, false);
    }
    rejected("Clock has no generic value", {variable_type::clock, 1},
        variable_value(boolean_values{{true}}), true);

    slave.omitInteger = true;
    rejected("reference exists only under other types", {variable_type::integer, 1},
        variable_value(integer_values{{1}}), false);
    slave.omitInteger = false;
    slave.arrayInteger = true;
    rejected("metadata declares an array", {variable_type::integer, 1},
        variable_value(integer_values{{1}}), false);
    slave.arrayInteger = false;
    const typed_value_reference references[] = {first, {variable_type::integer, 1}};
    BOOST_CHECK_THROW(slave.set_variable_values(references, gsl::make_span(&firstValue, 1)),
        std::invalid_argument);
    BOOST_TEST(slave.setterCalls == 0);
    double observed = -1.0;
    const value_reference reference = mock_slave::real_in_reference;
    slave.get_real_variables(gsl::make_span(&reference, 1), gsl::make_span(&observed, 1));
    BOOST_TEST(observed == 0.0);
}

// Empty batches avoid metadata; valid batches use one snapshot, preserve
// overlapping typed references and aliases, and leave numeric bounds to setters.
BOOST_AUTO_TEST_CASE(default_slave_batch_valid_and_empty)
{
    using namespace cosim;
    batch_validation_slave slave;
    slave.set_variable_values({}, {});
    BOOST_TEST(slave.modelRequests == 0);
    slave.addAlias = true;
    slave.realBounds = true;
    const typed_value_reference references[] = {
        {variable_type::real, 1}, {variable_type::integer, 1},
        {variable_type::boolean, 1}, {variable_type::string, 1}};
    const variable_value values[] = {
        variable_value(real_values{{7.0}}), variable_value(integer_values{{3}}),
        variable_value(boolean_values{{true}}), variable_value(string_values{{"ok"}})};
    slave.set_variable_values(references, values);
    BOOST_TEST(slave.modelRequests == 1);
    BOOST_TEST(slave.setterCalls == 4);
    const auto result = slave.get_variable_values(references);
    BOOST_REQUIRE_EQUAL(result.size(), 4U);
    for (std::size_t i = 0; i < result.size(); ++i) {
        BOOST_CHECK(result[i] == values[i]);
    }
}

// Prevalidation cannot roll back a successful write when a later typed setter
// itself fails; callers must not mistake it for a transaction.
BOOST_AUTO_TEST_CASE(default_slave_batch_later_setter_failure)
{
    using namespace cosim;
    batch_validation_slave slave;
    slave.failString = true;
    const typed_value_reference references[] = {
        {variable_type::real, 1}, {variable_type::string, 1}};
    const variable_value values[] = {
        variable_value(real_values{{7.0}}), variable_value(string_values{{"fail"}})};
    BOOST_CHECK_THROW(slave.set_variable_values(references, values), std::runtime_error);
    BOOST_TEST(slave.setterCalls == 2);
    double observed = 0.0;
    const value_reference reference = mock_slave::real_in_reference;
    slave.get_real_variables(gsl::make_span(&reference, 1), gsl::make_span(&observed, 1));
    BOOST_TEST(observed == 7.0);
}

#endif
