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

struct real_manipulator
{
    std::function<double(double)> f;
};

struct integer_manipulator
{
    std::function<int(int)> f;
};

struct boolean_manipulator
{
    std::function<bool(bool)> f;
};

struct string_manipulator
{
    std::function<std::string(std::string_view)> f;
};

struct variable_action
{
    simulator_index simulator;
    variable_causality causality;
    variable_index variable;
    std::variant<
        real_manipulator,
        integer_manipulator,
        boolean_manipulator,
        string_manipulator>
        manipulator;
};

struct event
{
    int id;
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
