#ifndef COSIM_TEST_CONNECTION_TYPE_TESTS_HPP
#define COSIM_TEST_CONNECTION_TYPE_TESTS_HPP

namespace
{
class integer_identity_function final : public cosim::function
{
public:
    cosim::function_description description() const override
    {
        cosim::function_description result;
        result.io_groups = {
            {"in", 1, {{"", cosim::variable_type::integer, cosim::variable_causality::input, 1}}},
            {"out", 1, {{"", cosim::variable_type::integer, cosim::variable_causality::output, 1}}}};
        return result;
    }

    void set_real(const cosim::function_io_reference&, double) override { throw std::logic_error("Invalid type"); }
    void set_integer(const cosim::function_io_reference&, int value) override { input_ = value; }
    void set_boolean(const cosim::function_io_reference&, bool) override { throw std::logic_error("Invalid type"); }
    void set_string(const cosim::function_io_reference&, std::string_view) override { throw std::logic_error("Invalid type"); }
    double get_real(const cosim::function_io_reference&) const override { throw std::logic_error("Invalid type"); }
    int get_integer(const cosim::function_io_reference&) const override { return output_; }
    bool get_boolean(const cosim::function_io_reference&) const override { throw std::logic_error("Invalid type"); }
    std::string_view get_string(const cosim::function_io_reference&) const override { throw std::logic_error("Invalid type"); }
    void calculate() override { output_ = input_; }

private:
    int input_ = 0;
    int output_ = 0;
};

class exposure_counting_simulator final : public cosim::slave_simulator
{
public:
    using cosim::slave_simulator::slave_simulator;

    void expose_for_getting(cosim::variable_type type, cosim::value_reference reference) override
    {
        ++getting;
        cosim::slave_simulator::expose_for_getting(type, reference);
    }

    void expose_for_setting(cosim::variable_type type, cosim::value_reference reference) override
    {
        ++setting;
        cosim::slave_simulator::expose_for_setting(type, reference);
    }

    int getting = 0;
    int setting = 0;
};

class recording_connection_algorithm final : public cosim::fixed_step_algorithm
{
public:
    recording_connection_algorithm()
        : cosim::fixed_step_algorithm(cosim::to_duration(0.1))
    {
    }

    void connect_variables(cosim::variable_id, cosim::variable_id) override { ++connections; }
    void connect_variables(cosim::variable_id, cosim::function_io_id) override { ++connections; }
    void connect_variables(cosim::function_io_id, cosim::variable_id) override { ++connections; }
#ifdef COSIM_TEST_FUNCTION_CONNECTIONS
    void connect_variables(cosim::function_io_id, cosim::function_io_id) override { ++connections; }
#endif

    int connections = 0;
};

template<typename Test>
void with_connection_algorithms(Test test)
{
    test(std::make_shared<cosim::fixed_step_algorithm>(cosim::to_duration(0.1)));
    test(std::make_shared<cosim::ecco_algorithm>(cosim::ecco_algorithm_params{
        0.99, cosim::to_duration(0.1), cosim::to_duration(0.01), cosim::to_duration(0.1),
        0.2, 1.5, 1e-6, 1e-6, 0.2, 0.15}));
}

int integer_input_value(const mock_slave& slave)
{
    const auto reference = mock_slave::integer_in_reference;
    int value = 0;
    slave.get_integer_variables(gsl::make_span(&reference, 1), gsl::make_span(&value, 1));
    return value;
}
} // namespace

// Execution must reject both mismatch directions before reaching the algorithm,
// while still forwarding valid same-type connections.
BOOST_AUTO_TEST_CASE(execution_validates_types_before_calling_algorithm)
{
    auto algorithm = std::make_shared<recording_connection_algorithm>();
    cosim::execution execution(cosim::time_point(), algorithm);
    const auto enumSource = execution.add_slave(std::make_shared<enumeration_mock_slave>(), "enum-source");
    const auto enumTarget = execution.add_slave(std::make_shared<enumeration_mock_slave>(), "enum-target");
    const auto intSource = execution.add_slave(std::make_shared<mock_slave>(), "int-source");
    const auto intTarget = execution.add_slave(std::make_shared<mock_slave>(), "int-target");
    const auto enumFunction = execution.add_function(std::make_shared<enumeration_identity_function>());
    const auto intFunction = execution.add_function(std::make_shared<integer_identity_function>());
    const auto enumeration = cosim::variable_type::enumeration;
    const auto integer = cosim::variable_type::integer;
    const cosim::variable_id enumOut{enumSource, enumeration, 101};
    const cosim::variable_id enumIn{enumTarget, enumeration, 100};
    const cosim::variable_id intOut{intSource, integer, mock_slave::integer_out_reference};
    const cosim::variable_id intIn{intTarget, integer, mock_slave::integer_in_reference};
    const cosim::function_io_id enumFunctionIn{enumFunction, enumeration, {0, 0, 0, 0}};
    const cosim::function_io_id enumFunctionOut{enumFunction, enumeration, {1, 0, 0, 0}};
    const cosim::function_io_id intFunctionIn{intFunction, integer, {0, 0, 0, 0}};
    const cosim::function_io_id intFunctionOut{intFunction, integer, {1, 0, 0, 0}};

    BOOST_CHECK_THROW(execution.connect_variables(enumOut, intIn), std::invalid_argument);
    BOOST_CHECK_THROW(execution.connect_variables(intOut, enumIn), std::invalid_argument);
    BOOST_CHECK_THROW(execution.connect_variables(enumOut, intFunctionIn), std::invalid_argument);
    BOOST_CHECK_THROW(execution.connect_variables(intOut, enumFunctionIn), std::invalid_argument);
    BOOST_CHECK_THROW(execution.connect_variables(enumFunctionOut, intIn), std::invalid_argument);
    BOOST_CHECK_THROW(execution.connect_variables(intFunctionOut, enumIn), std::invalid_argument);
    BOOST_CHECK_THROW(execution.connect_variables(
        intOut, (cosim::function_io_id{enumFunction, integer, {0, 0, 0, 0}})), std::logic_error);
    BOOST_CHECK_THROW(execution.connect_variables(
        (cosim::function_io_id{enumFunction, integer, {1, 0, 0, 0}}), intIn), std::logic_error);
#ifdef COSIM_TEST_FUNCTION_CONNECTIONS
    BOOST_CHECK_THROW(execution.connect_variables(enumFunctionOut, intFunctionIn), std::invalid_argument);
    BOOST_CHECK_THROW(execution.connect_variables(intFunctionOut, enumFunctionIn), std::invalid_argument);
#endif
    BOOST_TEST(algorithm->connections == 0);
    execution.connect_variables(enumOut, enumIn);
    execution.connect_variables(intOut, intIn);
    execution.connect_variables(enumOut, enumFunctionIn);
    execution.connect_variables(intOut, intFunctionIn);
    BOOST_TEST(algorithm->connections == 4);
}

// Both algorithms must reject mismatches across simulator and function paths;
// valid connections must still transfer values through an actual step.
BOOST_AUTO_TEST_CASE(execution_rejects_enumeration_integer_connections)
{
    with_connection_algorithms([](std::shared_ptr<cosim::algorithm> algorithm) {
        auto enumSource = std::make_shared<enumeration_mock_slave>();
        auto enumDirect = std::make_shared<enumeration_mock_slave>();
        auto enumThroughFunction = std::make_shared<enumeration_mock_slave>();
        auto intSource = std::make_shared<mock_slave>();
        auto intDirect = std::make_shared<mock_slave>();
        auto intThroughFunction = std::make_shared<mock_slave>();
        cosim::execution execution(cosim::time_point(), std::move(algorithm));
        const auto enumSourceId = execution.add_slave(enumSource, "enum-source");
        const auto enumDirectId = execution.add_slave(enumDirect, "enum-direct");
        const auto enumFunctionTargetId = execution.add_slave(enumThroughFunction, "enum-function-target");
        const auto intSourceId = execution.add_slave(intSource, "int-source");
        const auto intDirectId = execution.add_slave(intDirect, "int-direct");
        const auto intFunctionTargetId = execution.add_slave(intThroughFunction, "int-function-target");
        const auto enumFunctionId = execution.add_function(std::make_shared<enumeration_identity_function>());
        const auto intFunctionId = execution.add_function(std::make_shared<integer_identity_function>());
        const auto enumeration = cosim::variable_type::enumeration;
        const auto integer = cosim::variable_type::integer;
        const cosim::variable_id enumOut{enumSourceId, enumeration, 101};
        const cosim::variable_id enumIn{enumDirectId, enumeration, 100};
        const cosim::variable_id intOut{intSourceId, integer, mock_slave::integer_out_reference};
        const cosim::variable_id intIn{intDirectId, integer, mock_slave::integer_in_reference};
        const cosim::function_io_id enumFunctionIn{enumFunctionId, enumeration, {0, 0, 0, 0}};
        const cosim::function_io_id enumFunctionOut{enumFunctionId, enumeration, {1, 0, 0, 0}};
        const cosim::function_io_id intFunctionIn{intFunctionId, integer, {0, 0, 0, 0}};
        const cosim::function_io_id intFunctionOut{intFunctionId, integer, {1, 0, 0, 0}};

        BOOST_CHECK_THROW(execution.connect_variables(enumOut, intIn), std::invalid_argument);
        BOOST_CHECK_THROW(execution.connect_variables(intOut, enumIn), std::invalid_argument);
        BOOST_CHECK_THROW(execution.connect_variables(enumOut, intFunctionIn), std::invalid_argument);
        BOOST_CHECK_THROW(execution.connect_variables(intOut, enumFunctionIn), std::invalid_argument);
        BOOST_CHECK_THROW(execution.connect_variables(enumFunctionOut, intIn), std::invalid_argument);
        BOOST_CHECK_THROW(execution.connect_variables(intFunctionOut, enumIn), std::invalid_argument);
        BOOST_CHECK_THROW(execution.connect_variables(
            intOut, (cosim::function_io_id{enumFunctionId, integer, {0, 0, 0, 0}})), std::logic_error);
        BOOST_CHECK_THROW(execution.connect_variables(
            (cosim::function_io_id{enumFunctionId, integer, {1, 0, 0, 0}}), intIn), std::logic_error);
#ifdef COSIM_TEST_FUNCTION_CONNECTIONS
        BOOST_CHECK_THROW(execution.connect_variables(enumFunctionOut, intFunctionIn), std::invalid_argument);
        BOOST_CHECK_THROW(execution.connect_variables(intFunctionOut, enumFunctionIn), std::invalid_argument);
#endif

        execution.connect_variables(enumOut, enumIn);
        execution.connect_variables(intOut, intIn);
        execution.connect_variables(enumOut, enumFunctionIn);
        execution.connect_variables(intOut, intFunctionIn);
        execution.connect_variables(enumFunctionOut, cosim::variable_id{enumFunctionTargetId, enumeration, 100});
        execution.connect_variables(intFunctionOut,
            cosim::variable_id{intFunctionTargetId, integer, mock_slave::integer_in_reference});
        execution.set_variable_initial_value(
            cosim::variable_id{enumSourceId, enumeration, 100},
            cosim::variable_value(cosim::integer_values{{2}}));
        execution.set_variable_initial_value(
            cosim::variable_id{intSourceId, integer, mock_slave::integer_in_reference},
            cosim::variable_value(cosim::integer_values{{7}}));
        BOOST_REQUIRE(execution.simulate_until(cosim::to_time_point(0.1)));
        BOOST_TEST(enumDirect->value.value == 2);
        BOOST_TEST(enumThroughFunction->value.value == 2);
        BOOST_TEST(integer_input_value(*intDirect) == 7);
        BOOST_TEST(integer_input_value(*intThroughFunction) == 7);
    });
}

// Direct algorithm callers bypass execution, so they also need rejection before
// exposing any variables; successful same-type transfers remain supported.
BOOST_AUTO_TEST_CASE(direct_algorithms_reject_mismatched_connection_types_before_exposure)
{
    with_connection_algorithms([](std::shared_ptr<cosim::algorithm> algorithm) {
        auto enumSourceSlave = std::make_shared<enumeration_mock_slave>();
        auto enumTargetSlave = std::make_shared<enumeration_mock_slave>();
        auto intSourceSlave = std::make_shared<mock_slave>();
        auto intTargetSlave = std::make_shared<mock_slave>();
        exposure_counting_simulator enumSource(enumSourceSlave, "enum-source");
        exposure_counting_simulator enumTarget(enumTargetSlave, "enum-target");
        exposure_counting_simulator intSource(intSourceSlave, "int-source");
        exposure_counting_simulator intTarget(intTargetSlave, "int-target");
        enumeration_identity_function enumFunction;
        integer_identity_function intFunction;
        algorithm->add_simulator(0, &enumSource, cosim::duration::zero());
        algorithm->add_simulator(1, &enumTarget, cosim::duration::zero());
        algorithm->add_simulator(2, &intSource, cosim::duration::zero());
        algorithm->add_simulator(3, &intTarget, cosim::duration::zero());
        algorithm->add_function(0, &enumFunction);
        algorithm->add_function(1, &intFunction);
        const auto enumeration = cosim::variable_type::enumeration;
        const auto integer = cosim::variable_type::integer;
        const cosim::variable_id enumOut{0, enumeration, 101};
        const cosim::variable_id enumIn{1, enumeration, 100};
        const cosim::variable_id intOut{2, integer, mock_slave::integer_out_reference};
        const cosim::variable_id intIn{3, integer, mock_slave::integer_in_reference};
        const cosim::function_io_id enumFunctionIn{0, enumeration, {0, 0, 0, 0}};
        const cosim::function_io_id enumFunctionOut{0, enumeration, {1, 0, 0, 0}};
        const cosim::function_io_id intFunctionIn{1, integer, {0, 0, 0, 0}};
        const cosim::function_io_id intFunctionOut{1, integer, {1, 0, 0, 0}};

        BOOST_CHECK_THROW(algorithm->connect_variables(enumOut, intIn), std::invalid_argument);
        BOOST_CHECK_THROW(algorithm->connect_variables(intOut, enumIn), std::invalid_argument);
        BOOST_CHECK_THROW(algorithm->connect_variables(enumOut, intFunctionIn), std::invalid_argument);
        BOOST_CHECK_THROW(algorithm->connect_variables(intOut, enumFunctionIn), std::invalid_argument);
        BOOST_CHECK_THROW(algorithm->connect_variables(enumFunctionOut, intIn), std::invalid_argument);
        BOOST_CHECK_THROW(algorithm->connect_variables(intFunctionOut, enumIn), std::invalid_argument);
#ifdef COSIM_TEST_FUNCTION_CONNECTIONS
        BOOST_CHECK_THROW(algorithm->connect_variables(enumFunctionOut, intFunctionIn), std::invalid_argument);
        BOOST_CHECK_THROW(algorithm->connect_variables(intFunctionOut, enumFunctionIn), std::invalid_argument);
#endif
        BOOST_TEST(enumSource.getting + enumSource.setting == 0);
        BOOST_TEST(enumTarget.getting + enumTarget.setting == 0);
        BOOST_TEST(intSource.getting + intSource.setting == 0);
        BOOST_TEST(intTarget.getting + intTarget.setting == 0);

        algorithm->connect_variables(enumOut, enumIn);
        algorithm->connect_variables(intOut, intIn);
        enumSource.expose_for_setting(enumeration, 100);
        enumSource.set_variable_value(enumeration, 100,
            cosim::variable_value(cosim::integer_values{{2}}));
        intSource.expose_for_setting(integer, mock_slave::integer_in_reference);
        intSource.set_integer(mock_slave::integer_in_reference, 7);
        algorithm->setup(cosim::time_point(), std::nullopt);
        algorithm->initialize();
        algorithm->do_step(cosim::time_point());
        BOOST_TEST(enumTargetSlave->value.value == 2);
        BOOST_TEST(integer_input_value(*intTargetSlave) == 7);
        algorithm.reset();
    });
}

#endif
