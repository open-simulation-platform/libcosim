#include "mock_slave.hpp"

#include <cosim/algorithm.hpp>
#include <cosim/execution.hpp>
#include <cosim/log/simple.hpp>
#include <cosim/manipulator/manipulator.hpp>
#include <cosim/observer/last_value_observer.hpp>
#include <cosim/observer/time_series_observer.hpp>

#include <exception>
#include <functional>
#include <memory>
#include <stdexcept>

#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)


class test_manipulator : public cosim::manipulator
{
public:
    test_manipulator(const test_manipulator&) = delete;
    test_manipulator& operator=(const test_manipulator&) = delete;

    explicit test_manipulator(cosim::variable_id variable, cosim::time_point startTime, bool input)
        : variable_(variable)
        , input_(input)
    {
        double slope = 1.0;
        f_ = std::function<double(double, cosim::duration)>(
            [=, acc = 0.0](double original, cosim::duration deltaT) mutable {
                acc += slope * cosim::to_double_duration(deltaT, startTime);
                return original + acc;
            });
    }

    void simulator_added(cosim::simulator_index, cosim::manipulable* man, cosim::time_point) override
    {
        man_ = man;
    }

    void simulator_removed(cosim::simulator_index, cosim::time_point) override
    {
    }

    void step_commencing(cosim::time_point) override
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
    cosim::manipulable* man_;
    cosim::variable_id variable_;
    std::function<double(double, cosim::duration)> f_;
    bool initialized_ = false;
    bool input_;
};

int main()
{
    try {
        cosim::log::setup_simple_console_logging();
        cosim::log::set_global_output_level(cosim::log::debug);

        constexpr cosim::time_point startTime = cosim::to_time_point(0.0);
        constexpr cosim::duration stepSize = cosim::to_duration(0.1);

        auto execution = cosim::execution(startTime, std::make_unique<cosim::fixed_step_algorithm>(stepSize));

        auto observer = std::make_shared<cosim::last_value_observer>();
        execution.add_observer(observer);


        auto simIndex = execution.add_slave(std::make_unique<mock_slave>(),"mock");

        const auto input = cosim::variable_id{simIndex, cosim::variable_type::real, 1};
        const auto output = cosim::variable_id{simIndex, cosim::variable_type::real, 0};

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
