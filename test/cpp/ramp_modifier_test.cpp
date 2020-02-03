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
    test_manipulator(cse::variable_id variable)
        : variable_(variable)
    {
        double slope = 1.0;
        double acc = 0.0;
        double stepSize = 0.2;
        f_ = std::function<double(double)>([&](double original) {
            acc += slope * stepSize;
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

    void step_commencing(cse::time_point currentTime) override
    {
        if (currentTime <= startTime_) {
            man_->expose_for_setting(variable_.type, variable_.reference);
            man_->set_real_input_modifier(variable_.reference, f_);
        }
    }

    ~test_manipulator() noexcept override = default;

private:
    cse::time_point startTime_ = cse::to_time_point(2.0);
    cse::manipulable* man_;
    cse::variable_id variable_;
    std::function<double(double)> f_;
};

int main()
{
    try {
        cse::log::setup_simple_console_logging();
        cse::log::set_global_output_level(cse::log::debug);

        constexpr cse::time_point startTime = cse::to_time_point(0.0);
        constexpr cse::duration stepSize = cse::to_duration(1.0);

        auto execution = cse::execution(startTime, std::make_unique<cse::fixed_step_algorithm>(stepSize));

        auto observer = std::make_shared<cse::last_value_observer>();
        execution.add_observer(observer);


        auto simIndex = execution.add_slave(
            cse::make_pseudo_async(
                std::make_unique<mock_slave>()),
            "slave one",
            cse::to_duration(2.0));

        auto input = cse::variable_id{simIndex, cse::variable_type::real, 1};
        auto output = cse::variable_id{simIndex, cse::variable_type::real, 0};

        auto manipulator = std::make_shared<test_manipulator>(input);

        execution.add_manipulator(manipulator);


        double expectedValues[] = {1.0, 1.0, 3.0, 3.0, 5.0, 5.0};
        for (double expected : expectedValues) {
            execution.step();
            double value = -1.0;
            observer->get_real(output.simulator, gsl::make_span(&output.reference, 1), gsl::make_span(&value, 1));
            //            REQUIRE(expected == value);
            std::cout << "Expected value: " << expected << ", actual value: " << value << std::endl;
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
