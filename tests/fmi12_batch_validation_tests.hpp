#ifndef COSIM_TEST_FMI12_BATCH_VALIDATION_TESTS_HPP
#define COSIM_TEST_FMI12_BATCH_VALIDATION_TESTS_HPP

#include <type_traits>

// Each failure follows a valid Integer assignment: checking the old value
// proves validation completed before any FMU setter ran, not merely that the
// invalid entry eventually raised an exception.
void check_legacy_batch_prevalidation(
    cosim::slave& instance,
    cosim::value_reference integerReference,
    cosim::value_reference enumerationReference)
{
    using namespace cosim;
    const typed_value_reference first{variable_type::integer, integerReference};
    const typed_value_reference enumeration{variable_type::enumeration, enumerationReference};
    const int initial = 10;
    instance.set_integer_variables(gsl::make_span(&integerReference, 1), gsl::make_span(&initial, 1));
    const auto rejected = [&](const char* reason, typed_value_reference second,
                              variable_value badValue, bool inputError) {
        BOOST_TEST_CONTEXT(reason) {
            const typed_value_reference references[] = {first, second};
            const variable_value values[] = {variable_value(integer_values{{20}}), std::move(badValue)};
            if (inputError) {
                BOOST_CHECK_THROW(instance.set_variable_values(references, values), std::invalid_argument);
            } else {
                BOOST_CHECK_THROW(instance.set_variable_values(references, values), cosim::error);
            }
            int observed = 0;
            instance.get_integer_variables(gsl::make_span(&integerReference, 1), gsl::make_span(&observed, 1));
            BOOST_TEST(observed == initial);
        }
    };
    rejected("undeclared Enumeration item", enumeration, variable_value(enumeration_values{{enumeration_value{3}}}), false);
    rejected("mismatched value type", enumeration, variable_value(real_values{{1.0}}), true);
    rejected("unknown Integer reference", {variable_type::integer, 99999}, variable_value(integer_values{{1}}), false);
    rejected("unsupported exact-width type", {variable_type::int8, 99999}, variable_value(int8_values{{1}}), false);
    rejected("array on a scalar FMI interface", enumeration,
        variable_value(enumeration_values{{enumeration_value{1}, enumeration_value{2}}}, {2}), false);
    // Every supported scalar type must reject an unknown reference before
    // the first Integer write, not only Enumeration.
    const std::vector<variable_value> supported{
        variable_value(real_values{{1.0}}), variable_value(integer_values{{1}}),
        variable_value(boolean_values{{true}}), variable_value(string_values{{"x"}}),
        variable_value(enumeration_values{{enumeration_value{1}}})};
    for (const auto& value : supported) {
        BOOST_TEST_CONTEXT("unknown reference for " << to_text(value.type())) {
            rejected("typed reference", {value.type(), 99999}, value, false);
        }
        const auto wrong = value.type() == variable_type::real
            ? variable_value(boolean_values{{true}}) : variable_value(real_values{{1.0}});
        BOOST_TEST_CONTEXT("wrong value type for " << to_text(value.type())) {
            rejected("type mismatch", {value.type(), 99999}, wrong, true);
        }
        const auto array = std::visit([](const auto& source) {
            auto copy = source;
            typename std::decay_t<decltype(copy)>::value_type element = copy.values[0];
            copy.values.push_back(element);
            return variable_value(std::move(copy), {2});
        }, value.values());
        BOOST_TEST_CONTEXT("array for scalar type " << to_text(value.type())) {
            rejected("shape mismatch", {value.type(), 99999}, array, false);
        }
    }
    const typed_value_reference references[] = {first, enumeration};
    const variable_value oneValue[] = {variable_value(integer_values{{20}})};
    BOOST_CHECK_THROW(instance.set_variable_values(references, oneValue), std::invalid_argument);
    int observed = 0;
    instance.get_integer_variables(gsl::make_span(&integerReference, 1), gsl::make_span(&observed, 1));
    BOOST_TEST(observed == initial);

    // Empty and mixed valid batches remain supported, including scalar
    // Integer-to-Enumeration normalization.
    instance.set_variable_values({}, {});
    const variable_value valid[] = {
        variable_value(integer_values{{20}}), variable_value(integer_values{{2}})};
    instance.set_variable_values(references, valid);
    instance.get_integer_variables(gsl::make_span(&integerReference, 1), gsl::make_span(&observed, 1));
    BOOST_TEST(observed == 20);
    const auto result = instance.get_variable_values(gsl::make_span(&enumeration, 1));
    BOOST_REQUIRE_EQUAL(result.size(), 1U);
    BOOST_TEST(std::get<enumeration_values>(result[0].values()).values[0].value == 2);
}

#endif
