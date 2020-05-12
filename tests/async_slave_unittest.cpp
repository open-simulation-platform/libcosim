#define BOOST_TEST_MODULE async_slave.hpp unittests
#include <cosim/async_slave.hpp>
#include <cosim/exception.hpp>

#include <boost/test/unit_test.hpp>

#include <stdexcept>

using namespace cosim;


class test_slave : public slave
{
public:
    cosim::model_description model_description() const override { return cosim::model_description(); }

    void setup(time_point startTime, std::optional<time_point> stopTime, std::optional<double>) override
    {
        if (startTime > *stopTime) throw std::logic_error("Invalid time interval");
    }

    void start_simulation() override { }
    void end_simulation() override { }
    step_result do_step(time_point, duration) override { return step_result::complete; }

    void get_real_variables(gsl::span<const value_reference>, gsl::span<double>) const override { }
    void get_integer_variables(gsl::span<const value_reference>, gsl::span<int>) const override { }
    void get_boolean_variables(gsl::span<const value_reference>, gsl::span<bool>) const override { }
    void get_string_variables(gsl::span<const value_reference>, gsl::span<std::string>) const override { }

    void set_real_variables(
        gsl::span<const value_reference>,
        gsl::span<const double> values) override
    {
        if (!values.empty() && values[0] == 0.0) throw nonfatal_bad_value("real");
    }
    void set_integer_variables(
        gsl::span<const value_reference> variables,
        gsl::span<const int> values) override
    {
        if (!values.empty()) {
            if (variables[0] != 0) throw std::logic_error("Invalid variable reference");
            if (values[0] == 0) throw nonfatal_bad_value("integer");
        }
    }
    void set_boolean_variables(
        gsl::span<const value_reference>,
        gsl::span<const bool> values) override
    {
        if (!values.empty() && !values[0]) throw nonfatal_bad_value("boolean");
    }
    void set_string_variables(
        gsl::span<const value_reference>,
        gsl::span<const std::string> values) override
    {
        if (!values.empty() && values[0].empty()) throw nonfatal_bad_value("string");
    }
};


void test_error_handling_1(async_slave& as)
{
    BOOST_REQUIRE(as.state() == slave_state::created);
    auto f1 = as.setup(to_time_point(0.0), to_time_point(-1.0), std::nullopt);
    BOOST_REQUIRE(as.state() == slave_state::indeterminate);
    BOOST_REQUIRE_THROW(f1.get(), std::logic_error);
    BOOST_REQUIRE(as.state() == slave_state::error);
}

void test_error_handling_2(async_slave& as)
{
    as.setup(to_time_point(0.0), to_time_point(1.0), std::nullopt).get();
    auto f2 = as.start_simulation();
    BOOST_REQUIRE(as.state() == slave_state::indeterminate);
    f2.get();
    BOOST_REQUIRE(as.state() == slave_state::simulation);

    value_reference realIndex = 0;
    double realValue = 1.0;
    value_reference integerIndex = 0;
    int integerValue = 1;
    value_reference booleanIndex = 0;
    bool booleanValue = true;
    value_reference stringIndex = 0;
    std::string stringValue = "foo";
    auto f3 = as.set_variables(
        gsl::make_span(&realIndex, 1), gsl::make_span(&realValue, 1),
        gsl::make_span(&integerIndex, 1), gsl::make_span(&integerValue, 1),
        gsl::make_span(&booleanIndex, 1), gsl::make_span(&booleanValue, 1),
        gsl::make_span(&stringIndex, 1), gsl::make_span(&stringValue, 1));
    BOOST_REQUIRE(as.state() == slave_state::indeterminate);
    f3.get();
    BOOST_REQUIRE(as.state() == slave_state::simulation);

    // Check that `nonfatal_bad_value` errors are accumulated across
    // variable types.
    realValue = 0.0;
    integerValue = 0;
    booleanValue = false;
    stringValue.clear();
    auto f4 = as.set_variables(
        gsl::make_span(&realIndex, 1), gsl::make_span(&realValue, 1),
        gsl::make_span(&integerIndex, 1), gsl::make_span(&integerValue, 1),
        gsl::make_span(&booleanIndex, 1), gsl::make_span(&booleanValue, 1),
        gsl::make_span(&stringIndex, 1), gsl::make_span(&stringValue, 1));
    BOOST_REQUIRE(as.state() == slave_state::indeterminate);
    BOOST_REQUIRE_EXCEPTION(
        f4.get(),
        nonfatal_bad_value,
        [](const nonfatal_bad_value& e) {
            // Check that the exception message contains the error message from
            // each individual operation.
            std::string msg = e.what();
            return msg.find("real") != std::string::npos &&
                msg.find("integer") != std::string::npos &&
                msg.find("boolean") != std::string::npos &&
                msg.find("string") != std::string::npos;
        });
    BOOST_REQUIRE(as.state() == slave_state::simulation);

    // Check that other exceptions are *not* accumulated.
    integerIndex = 1;
    auto f5 = as.set_variables(
        gsl::make_span(&realIndex, 1), gsl::make_span(&realValue, 1),
        gsl::make_span(&integerIndex, 1), gsl::make_span(&integerValue, 1),
        gsl::make_span(&booleanIndex, 1), gsl::make_span(&booleanValue, 1),
        gsl::make_span(&stringIndex, 1), gsl::make_span(&stringValue, 1));
    BOOST_REQUIRE(as.state() == slave_state::indeterminate);
    BOOST_REQUIRE_THROW(f5.get(), std::logic_error);
    BOOST_REQUIRE(as.state() == slave_state::error);
}


BOOST_AUTO_TEST_CASE(pseudo_async_slave_error_handling)
{
    auto as1 = make_pseudo_async(std::make_shared<test_slave>());
    test_error_handling_1(*as1);
    auto as2 = make_pseudo_async(std::make_shared<test_slave>());
    test_error_handling_2(*as2);
}


BOOST_AUTO_TEST_CASE(background_thread_slave_error_handling)
{
    auto as1 = make_background_thread_slave(std::make_shared<test_slave>());
    test_error_handling_1(*as1);
    auto as2 = make_background_thread_slave(std::make_shared<test_slave>());
    test_error_handling_2(*as2);
}
