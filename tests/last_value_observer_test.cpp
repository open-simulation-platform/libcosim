#include "mock_slave.hpp"

#include <cosim/algorithm.hpp>
#include <cosim/execution.hpp>
#include <cosim/log/logger.hpp>
#include <cosim/manipulator/override_manipulator.hpp>
#include <cosim/observer/last_value_observer.hpp>

#include <cmath>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>


// A helper macro to test various assertions
#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

int main()
{
    try {
        cosim::log::set_logging_level(cosim::log::level::debug);

        constexpr cosim::time_point startTime;
        constexpr cosim::duration stepSize = cosim::to_duration(0.5);

        // Set up execution
        auto execution = cosim::execution(
            startTime,
            std::make_unique<cosim::fixed_step_algorithm>(stepSize));

        auto observer = std::make_shared<cosim::last_value_observer>();
        execution.add_observer(observer);

        const auto sim = execution.add_slave(
            std::make_unique<mock_slave>(
                [](double x) { return x + 1.234; },
                [](int i) { return i + 1; },
                [](bool b) { return !b; },
                [](std::string_view s) { return std::string(s) + std::string("bar"); }),
            "slave");

        execution.step();

        const cosim::value_reference realOutRef = mock_slave::real_out_reference;
        const cosim::value_reference realInRef = mock_slave::real_in_reference;
        const cosim::value_reference intOutRef = mock_slave::integer_out_reference;
        const cosim::value_reference intInRef = mock_slave::integer_in_reference;
        const cosim::value_reference boolOutRef = mock_slave::boolean_out_reference;
        const cosim::value_reference boolInRef = mock_slave::boolean_in_reference;
        const cosim::value_reference stringOutRef = mock_slave::string_out_reference;
        const cosim::value_reference stringInRef = mock_slave::string_in_reference;

        double realInValue = -1.0;
        double realOutValue = -1.0;
        observer->get_real(sim, gsl::make_span(&realInRef, 1), gsl::make_span(&realInValue, 1));
        observer->get_real(sim, gsl::make_span(&realOutRef, 1), gsl::make_span(&realOutValue, 1));
        REQUIRE(std::fabs(realInValue - 0.0) < 1.0e-9);
        REQUIRE(std::fabs(realOutValue - 1.234) < 1.0e-9);

        int intInValue = -1;
        int intOutValue = -1;
        observer->get_integer(sim, gsl::make_span(&intInRef, 1), gsl::make_span(&intInValue, 1));
        observer->get_integer(sim, gsl::make_span(&intOutRef, 1), gsl::make_span(&intOutValue, 1));
        REQUIRE(intInValue == 0);
        REQUIRE(intOutValue == 1);

        bool boolInValue = true;
        bool boolOutValue = false;
        observer->get_boolean(sim, gsl::make_span(&boolInRef, 1), gsl::make_span(&boolInValue, 1));
        observer->get_boolean(sim, gsl::make_span(&boolOutRef, 1), gsl::make_span(&boolOutValue, 1));
        REQUIRE(boolInValue == false);
        REQUIRE(boolOutValue == true);

        std::string stringInValue;
        std::string stringOutValue;
        observer->get_string(sim, gsl::make_span(&stringInRef, 1), gsl::make_span(&stringInValue, 1));
        observer->get_string(sim, gsl::make_span(&stringOutRef, 1), gsl::make_span(&stringOutValue, 1));
        REQUIRE(stringInValue == "");
        REQUIRE(stringOutValue == "bar");

        auto manipulator = std::make_shared<cosim::override_manipulator>();
        execution.add_manipulator(manipulator);

        manipulator->override_real_variable(sim, realInRef, 2.0);
        manipulator->override_integer_variable(sim, intInRef, 2);
        manipulator->override_boolean_variable(sim, boolInRef, true);
        manipulator->override_string_variable(sim, stringInRef, "foo");

        execution.step();

        observer->get_real(sim, gsl::make_span(&realInRef, 1), gsl::make_span(&realInValue, 1));
        observer->get_real(sim, gsl::make_span(&realOutRef, 1), gsl::make_span(&realOutValue, 1));
        REQUIRE(std::fabs(realInValue - 2.0) < 1.0e-9);
        REQUIRE(std::fabs(realOutValue - 3.234) < 1.0e-9);

        observer->get_integer(sim, gsl::make_span(&intInRef, 1), gsl::make_span(&intInValue, 1));
        observer->get_integer(sim, gsl::make_span(&intOutRef, 1), gsl::make_span(&intOutValue, 1));
        REQUIRE(intInValue == 2);
        REQUIRE(intOutValue == 3);

        observer->get_boolean(sim, gsl::make_span(&boolInRef, 1), gsl::make_span(&boolInValue, 1));
        observer->get_boolean(sim, gsl::make_span(&boolOutRef, 1), gsl::make_span(&boolOutValue, 1));
        REQUIRE(boolInValue == true);
        REQUIRE(boolOutValue == false);

        observer->get_string(sim, gsl::make_span(&stringInRef, 1), gsl::make_span(&stringInValue, 1));
        observer->get_string(sim, gsl::make_span(&stringOutRef, 1), gsl::make_span(&stringOutValue, 1));
        REQUIRE(stringInValue == "foo");
        REQUIRE(stringOutValue == "foobar");


    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
