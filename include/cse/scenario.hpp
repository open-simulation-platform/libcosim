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

using manipulators = std::variant<
    real_manipulator,
    integer_manipulator,
    boolean_manipulator,
    string_manipulator>;

struct variable_action
{
    simulator_index simulator;
    variable_index variable;
    manipulators manipulator;
    bool is_input;
};

struct event
{
    time_point time;
    variable_action action;
};

struct scenario
{
    std::vector<event> events;
    std::optional<time_point> end;
};

} // namespace scenario

} // namespace cse

#endif //CSECORE_SCENARIO_HPP
