#define BOOST_TEST_MODULE modifiers / gain_modifier.hpp unittests

#include "mock_slave.hpp"

#include <cse/algorithm.hpp>
#include <cse/async_slave.hpp>
#include <cse/connection.hpp>
#include <cse/execution.hpp>
#include <cse/modifier/gain_modifier.hpp>
#include <cse/observer/last_value_observer.hpp>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_CASE(test_gain_modifier)
{

    auto algorithm = std::make_shared<cse::fixed_step_algorithm>(cse::to_duration(0.1));
    auto execution = cse::execution({}, algorithm);

    auto observer = std::make_shared<cse::last_value_observer>();
    execution.add_observer(observer);

    execution.add_slave(
        cse::make_pseudo_async(std::make_unique<mock_slave>(
            [](double) { return 5; },
            [](int) { return 5; })),
        "slave1");

    execution.add_slave(
        cse::make_pseudo_async(std::make_unique<mock_slave>()), "slave2");

    {
        cse::variable_id realOut{0, cse::variable_type::real, 0};
        cse::variable_id realIn{1, cse::variable_type::real, 1};
        auto scalarConnection = std::make_shared<cse::scalar_connection>(realOut, realIn);
        scalarConnection->add_modifier(std::make_shared<cse::gain_modifier>(1.5));
        execution.add_connection(scalarConnection);
    }

    {
        cse::variable_id intOut{0, cse::variable_type::integer, 0};
        cse::variable_id intIn{1, cse::variable_type::integer, 1};
        auto scalarConnection = std::make_shared<cse::scalar_connection>(intOut, intIn);
        scalarConnection->add_modifier(std::make_shared<cse::gain_modifier>(1.5));
        execution.add_connection(scalarConnection);
    }

    execution.step();
    execution.step();

    {
        double realInValue;
        cse::value_reference vr = 1;
        observer->get_real(1, gsl::make_span(&vr, 1), gsl::make_span(&realInValue, 1));

        BOOST_TEST(realInValue == 7.5);
    }

    {
        int intInValue;
        cse::value_reference vr = 1;
        observer->get_integer(1, gsl::make_span(&vr, 1), gsl::make_span(&intInValue, 1));

        BOOST_TEST(intInValue == 8);
    }

}
