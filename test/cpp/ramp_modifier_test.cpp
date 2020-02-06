#include "mock_slave.hpp"

#include <cse/algorithm.hpp>
#include <cse/async_slave.hpp>
#include <cse/execution.hpp>
#include <cse/log/simple.hpp>
#include <cse/manipulator/manipulator.hpp>
#include <cse/observer/last_value_observer.hpp>
#include <cse/observer/time_series_observer.hpp>

#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>

#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)


class test_manipulator : public cse::manipulator
{
public:
    test_manipulator(const test_manipulator&) = delete;
    test_manipulator& operator=(const test_manipulator&) = delete;

    explicit test_manipulator(cse::variable_id variable, cse::time_point startTime, bool input)
        : variable_(variable)
        , input_(input)
    {
        double slope = 1.0;
        f_ = std::function<double(double, cse::duration)>(
            [=, acc = 0.0](double original, cse::duration deltaT) mutable {
                acc += slope * cse::to_double_duration(deltaT, startTime);
                return original + acc;
            });
    }

    void simulator_added(cse::simulator_index, cse::manipulable* man, cse::time_point) override
    {
        man_ = man;
    }

    void simulator_removed(cse::simulator_index, cse::time_point) override
    {
    }

    void step_commencing(cse::time_point) override
    {
        if (!initialized_) {
            if (input_) {
                man_->expose_for_setting(variable_.type, variable_.reference);
                man_->set_real_input_modifier(variable_.reference, f_);
            } else {
                man_->expose_for_getting(variable_.type, variable_.reference);
                man_->set_real_output_modifier(variable_.reference, f_);
            }
            initialized_ = true;
        }
    }

    ~test_manipulator() noexcept override = default;

private:
    cse::manipulable* man_;
    cse::variable_id variable_;
    std::function<double(double, cse::duration)> f_;
    bool initialized_ = false;
    bool input_;
};

int main()
{
    try {
        cse::log::setup_simple_console_logging();
        cse::log::set_global_output_level(cse::log::debug);

        constexpr cse::time_point startTime = cse::to_time_point(0.0);
        constexpr cse::duration stepSize = cse::to_duration(0.1);

        auto execution = cse::execution(startTime, std::make_unique<cse::fixed_step_algorithm>(stepSize));

        auto observer = std::make_shared<cse::last_value_observer>();
        execution.add_observer(observer);


        auto simIndex = execution.add_slave(
            cse::make_pseudo_async(
                std::make_unique<mock_slave>()),
            "mock");

        const auto input = cse::variable_id{simIndex, cse::variable_type::real, 1};
        const auto output = cse::variable_id{simIndex, cse::variable_type::real, 0};

        auto inputManipulator = std::make_shared<test_manipulator>(input, startTime, true);
        auto outputManipulator = std::make_shared<test_manipulator>(output, startTime, false);

        execution.add_manipulator(inputManipulator);
        execution.add_manipulator(outputManipulator);

        execution.set_real_initial_value(input.simulator, input.reference, 0.0);

        double expectedInputValues[] = {0.1, 0.2, 0.3, 0.4, 0.5, 0.6};
        double expectedOutputValues[] = {0.2, 0.4, 0.6, 0.8, 1.0, 1.2};
        for (int i = 0; i < 6; i++) {
            execution.step();

            double expectedInputValue = expectedInputValues[i];
            double inputValue = -1.0;
            observer->get_real(input.simulator, gsl::make_span(&input.reference, 1), gsl::make_span(&inputValue, 1));
            std::cout << "Expected input value: " << expectedInputValue << ", actual value: " << inputValue << std::endl;
            REQUIRE(std::fabs(expectedInputValue - inputValue) < 1.0e-9);

            double expectedOutputValue = expectedOutputValues[i];
            double outputValue = -1.0;
            observer->get_real(output.simulator, gsl::make_span(&output.reference, 1), gsl::make_span(&outputValue, 1));
            std::cout << "Expected output value: " << expectedOutputValue << ", actual value: " << outputValue << std::endl;
            REQUIRE(std::fabs(expectedOutputValue - outputValue) < 1.0e-9);
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
