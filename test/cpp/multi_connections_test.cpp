#include "mock_slave.hpp"

#include <cse/algorithm.hpp>
#include <cse/async_slave.hpp>
#include <cse/connection.hpp>
#include <cse/execution.hpp>
#include <cse/log.hpp>
#include <cse/observer/last_value_observer.hpp>

#include <cmath>
#include <exception>
#include <memory>
#include <stdexcept>


// A helper macro to test various assertions
#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)

int main()
{
    try {
        constexpr int numSlaves = 7;
        constexpr cse::time_point startTime;
        constexpr cse::time_point endTime = cse::to_time_point(1.0);
        constexpr cse::duration stepSize = cse::to_duration(0.1);

        cse::log::set_global_output_level(cse::log::level::debug);

        auto algorithm = std::make_shared<cse::fixed_step_algorithm>(stepSize);
        auto execution = cse::execution(
            startTime,
            algorithm);

        auto observer = std::make_shared<cse::last_value_observer>();
        execution.add_observer(observer);

        const cse::variable_index realOutIndex = 0;
        const cse::variable_index realInIndex = 1;

        // Add slaves to it
        for (int i = 0; i < numSlaves; ++i) {
            execution.add_slave(
                cse::make_pseudo_async(std::make_unique<mock_slave>([](double x) { return x + 1.0; })),
                "slave" + std::to_string(i));
        }

        cse::variable_id sum_destination{3, cse::variable_type::real, 1};
        std::vector<cse::variable_id> sources;
        sources.push_back({0, cse::variable_type::real, 0});
        sources.push_back({1, cse::variable_type::real, 0});
        sources.push_back({2, cse::variable_type::real, 0});

        auto sumConnection = std::make_shared<cse::sum_connection>(sources, sum_destination);
        execution.add_connection(sumConnection);

        cse::variable_id out3{3, cse::variable_type::real, 0};
        cse::variable_id in4{4, cse::variable_type::real, 1};
        auto scalarConnection = std::make_shared<cse::scalar_connection>(out3, in4);
        execution.add_connection(scalarConnection);

        cse::variable_id out0{3, cse::variable_type::real, 0};
        cse::variable_id in5{5, cse::variable_type::real, 1};
        auto scalarConnection2 = std::make_shared<cse::scalar_connection>(out0, in5);
        execution.add_connection(scalarConnection2);

        cse::variable_id out5{5, cse::variable_type::real, 0};
        cse::variable_id in6{6, cse::variable_type::real, 1};
        auto scalarConnection3 = std::make_shared<cse::scalar_connection>(out5, in6);
        execution.add_connection(scalarConnection3);

        bool exceptionThrown = false;
        try {
            cse::variable_id out{0, cse::variable_type::real, 0};
            cse::variable_id in{5, cse::variable_type::real, 1};
            auto scalarConnectionToNonExistingSimulator = std::make_shared<cse::scalar_connection>(out, in);
            execution.add_connection(scalarConnectionToNonExistingSimulator);
        } catch (const std::exception& e) {
            std::cout << "Caught exception as expected: " << e.what() << std::endl;
            exceptionThrown = true;
        }
        REQUIRE(exceptionThrown);

        exceptionThrown = false;
        try {
            cse::variable_id out{0, cse::variable_type::real, 4};
            cse::variable_id in{1, cse::variable_type::real, 1};
            auto scalarConnectionFromNonExistingVariable = std::make_shared<cse::scalar_connection>(out, in);
            execution.add_connection(scalarConnectionFromNonExistingVariable);
        } catch (const std::exception& e) {
            std::cout << "Caught exception as expected: " << e.what() << std::endl;
            exceptionThrown = true;
        }
        REQUIRE(exceptionThrown);

        exceptionThrown = false;
        try {
            cse::variable_id out{0, cse::variable_type::real, 1};
            cse::variable_id in{1, cse::variable_type::real, 1};
            auto scalarConnectionFromVariableWithWrongCausality = std::make_shared<cse::scalar_connection>(out, in);
            execution.add_connection(scalarConnectionFromVariableWithWrongCausality);
        } catch (const std::exception& e) {
            std::cout << "Caught exception as expected: " << e.what() << std::endl;
            exceptionThrown = true;
        }
        REQUIRE(exceptionThrown);

        exceptionThrown = false;
        try {
            cse::variable_id out{0, cse::variable_type::real, 0};
            cse::variable_id in{1, cse::variable_type::real, 0};
            auto scalarConnectionToVariableWithWrongCausality = std::make_shared<cse::scalar_connection>(out, in);
            execution.add_connection(scalarConnectionToVariableWithWrongCausality);
        } catch (const std::exception& e) {
            std::cout << "Caught exception as expected: " << e.what() << std::endl;
            exceptionThrown = true;
        }
        REQUIRE(exceptionThrown);

        exceptionThrown = false;
        try {
            cse::variable_id out{0, cse::variable_type::real, 0};
            cse::variable_id in{4, cse::variable_type::real, 1};
            auto scalarConnectionToAlreadyConnectedDestination = std::make_shared<cse::scalar_connection>(out, in);
            execution.add_connection(scalarConnectionToAlreadyConnectedDestination);
        } catch (const std::exception& e) {
            std::cout << "Caught exception as expected: " << e.what() << std::endl;
            exceptionThrown = true;
        }
        REQUIRE(exceptionThrown);

        auto simResult = execution.simulate_until(endTime);
        REQUIRE(simResult.get());

        double realInValue = -1.0;
        double expectedRealInValue = 3.0;
        observer->get_real(3, gsl::make_span(&realInIndex, 1), gsl::make_span(&realInValue, 1));
        REQUIRE(std::fabs(expectedRealInValue - realInValue) < 1.0e-9);

        double realOutValue = -1.0;
        double expectedRealOutValue = 5.0;
        observer->get_real(4, gsl::make_span(&realOutIndex, 1), gsl::make_span(&realOutValue, 1));
        REQUIRE(std::fabs(expectedRealOutValue - realOutValue) < 1.0e-9);

        REQUIRE(execution.get_connections().size() == 4);

        execution.remove_connection(sum_destination);

        REQUIRE(execution.get_connections().size() == 3);


        std::vector<cse::variable_id> newSources;
        newSources.push_back({0, cse::variable_type::real, 0});
        newSources.push_back({1, cse::variable_type::real, 0});

        auto modifiedSumConnection = std::make_shared<cse::sum_connection>(newSources, sum_destination);
        execution.add_connection(modifiedSumConnection);

        constexpr cse::time_point newEndTime = cse::to_time_point(2.0);

        simResult = execution.simulate_until(newEndTime);
        REQUIRE(simResult.get());

        expectedRealOutValue = 4.0;
        observer->get_real(4, gsl::make_span(&realOutIndex, 1), gsl::make_span(&realOutValue, 1));
        REQUIRE(std::fabs(expectedRealOutValue - realOutValue) < 1.0e-9);

        REQUIRE(execution.get_connections().size() == 4);

        // Just check that it doesn't crash
        algorithm->remove_simulator(5);


    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
