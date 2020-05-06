#define BOOST_TEST_MODULE system_structure unittests
#include "mock_slave.hpp"

#include <cse/algorithm/fixed_step_algorithm.hpp>
#include <cse/exception.hpp>
#include <cse/execution.hpp>
#include <cse/function/linear_transformation.hpp>
#include <cse/observer/last_value_observer.hpp>
#include <cse/system_structure.hpp>

#include <boost/test/unit_test.hpp>

#include <algorithm>


class mock_model : public cosim::model
{
public:
    std::shared_ptr<const cosim::model_description> description()
        const noexcept override
    {
        if (!nextSlave_) nextSlave_ = std::make_shared<mock_slave>();
        return std::make_shared<cosim::model_description>(
            nextSlave_->model_description());
    }

    std::shared_ptr<cosim::async_slave> instantiate(std::string_view /*name*/)
        override
    {
        if (!nextSlave_) nextSlave_ = std::make_shared<mock_slave>();
        return cosim::make_pseudo_async(std::move(nextSlave_));
    }

private:
    mutable std::shared_ptr<mock_slave> nextSlave_;
};


BOOST_AUTO_TEST_CASE(system_structure_basic_use)
{
    // Some test parameters
    constexpr auto startTime = cosim::time_point();
    constexpr auto stopTime = cosim::time_point(std::chrono::seconds(1));
    constexpr auto timeStep = std::chrono::milliseconds(100);
    constexpr auto offset = 2.0;
    constexpr auto factor = 3.0;

    // Get a model and function type
    const auto model = std::make_shared<mock_model>();
    const auto func = std::make_shared<cosim::linear_transformation_function_type>();
    const auto funcParams = cosim::function_parameter_value_map{
        std::make_pair(cosim::linear_transformation_function_type::offset_parameter_index, offset),
        std::make_pair(cosim::linear_transformation_function_type::factor_parameter_index, factor)};

    // Set up a system structure, check basic functionality
    cosim::system_structure ss;

    ss.add_entity("simA", model);
    ss.add_entity("simB", model, timeStep * 2);
    ss.add_entity("func", func, funcParams);
    ss.add_entity("simC", model);
    BOOST_CHECK_THROW(ss.add_entity("simB", model), cosim::error); // simB exists
    BOOST_CHECK_THROW(ss.add_entity("func", func, funcParams), cosim::error); // func exists
    BOOST_CHECK_THROW(ss.add_entity("", model), cosim::error); // invalid name
    BOOST_CHECK_THROW(ss.add_entity("simB", model, -timeStep), cosim::error); // negative step size

    ss.connect_variables({"simA", "realOut"}, {"simB", "realIn"}); // sim-sim connection
    ss.connect_variables({"simA", "realOut"}, {"simA", "realIn"}); // sim self connection
    ss.connect_variables({"simB", "realOut"}, {"func", "in", 0, "", 0}); // sim-func connection
    ss.connect_variables({"func", "out", 0, "", 0}, {"simC", "realIn"}); // func-sim connection
    BOOST_CHECK_THROW(
        ss.connect_variables({"simB", "realOut"}, {"simB", "realIn"}),
        cosim::error); // simB.realIn already connected
    BOOST_CHECK_THROW(
        ss.connect_variables({"simC", "realOut"}, {"func", "in", 0, "", 0}),
        cosim::error); // func.in already connected
    BOOST_CHECK_THROW(
        ss.connect_variables({"simA", "realOut"}, {"simB", "intIn"}),
        cosim::error); // incompatible variables

    const auto entities = ss.entities();
    BOOST_TEST_REQUIRE(std::distance(entities.begin(), entities.end()) == 4);
    const auto conns = ss.connections();
    BOOST_TEST_REQUIRE(std::distance(conns.begin(), conns.end()) == 4);

    // Initial values
    constexpr auto initialInput = 11.0;
    cosim::variable_value_map initialValues;
    cosim::add_variable_value(initialValues, ss, {"simA", "realIn"}, initialInput);
    BOOST_CHECK_THROW(
        cosim::add_variable_value(initialValues, ss, {"noneSuch", "realIn"}, 1.0),
        cosim::error);
    BOOST_CHECK_THROW(
        cosim::add_variable_value(initialValues, ss, {"simA", "noneSuch"}, 1.0),
        cosim::error);
    BOOST_CHECK_THROW(
        cosim::add_variable_value(initialValues, ss, {"simA", "realIn"}, "a string"),
        cosim::error);
    BOOST_CHECK(initialValues.size() == 1);

    // Set up and run an execution to verify that the system structure
    // turns out as intended.
    auto execution = cosim::execution(
        startTime, std::make_shared<cosim::fixed_step_algorithm>(timeStep));
    const auto obs = std::make_shared<cosim::last_value_observer>();
    execution.add_observer(obs);

    const auto entityIndexes =
        cosim::inject_system_structure(execution, ss, initialValues);
    BOOST_CHECK(entityIndexes.simulators.size() == 3);
    BOOST_CHECK(entityIndexes.functions.size() == 1);

    BOOST_CHECK(execution.simulate_until(stopTime).get());

    const auto realOutRef = mock_slave::real_out_reference;
    double realOutA = -1.0, realOutB = -1.0, realOutC = -1.0;
    obs->get_real(
        entityIndexes.simulators.at("simA"),
        gsl::make_span(&realOutRef, 1),
        gsl::make_span(&realOutA, 1));
    obs->get_real(
        entityIndexes.simulators.at("simB"),
        gsl::make_span(&realOutRef, 1),
        gsl::make_span(&realOutB, 1));
    obs->get_real(
        entityIndexes.simulators.at("simC"),
        gsl::make_span(&realOutRef, 1),
        gsl::make_span(&realOutC, 1));
    BOOST_CHECK(realOutA == initialInput);
    BOOST_CHECK(realOutB == initialInput);
    BOOST_CHECK(realOutC == offset + factor * initialInput);
}
