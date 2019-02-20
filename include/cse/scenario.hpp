#ifndef CSECORE_SCENARIO_HPP
#define CSECORE_SCENARIO_HPP

#include <cse/execution.hpp>
#include <cse/model.hpp>

#include <functional>
#include <string>
#include <variant>
#include <vector>

namespace cse
{

namespace scenario
{

struct time_trigger
{
    time_point trigger_point;
};

struct real_input_manipulator
{
    std::function<double(double)> f;
};

struct real_output_manipulator
{
    std::function<double(double)> f;
};

struct integer_input_manipulator
{
    std::function<int(int)> f;
};

struct integer_output_manipulator
{
    std::function<int(int)> f;
};

struct boolean_input_manipulator
{
    std::function<bool(bool)> f;
};

struct boolean_output_manipulator
{
    std::function<bool(bool)> f;
};

struct string_input_manipulator
{
    std::function<std::string(std::string_view)> f;
};

struct string_output_manipulator
{
    std::function<std::string(std::string_view)> f;
};

struct variable_action
{
    simulator_index simulator;
    variable_index variable;
    std::variant<
        real_input_manipulator,
        real_output_manipulator,
        integer_input_manipulator,
        integer_output_manipulator,
        boolean_input_manipulator,
        boolean_output_manipulator,
        string_input_manipulator,
        string_output_manipulator>
        manipulator;
};

struct event
{
    time_trigger trigger;
    variable_action action;
};

struct scenario
{
    std::vector<event> events;
};

} // namespace scenario

} // namespace cse

#endif //CSECORE_SCENARIO_HPP
