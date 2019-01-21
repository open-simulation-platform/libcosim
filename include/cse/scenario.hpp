#ifndef CSECORE_SCENARIO_HPP
#define CSECORE_SCENARIO_HPP

#include <string>
#include <variant>
#include <vector>

#include <cse/execution.hpp>
#include <cse/model.hpp>

namespace cse
{

namespace scenario
{

struct time_trigger
{
    time_point trigger_point;
};

struct variable_action
{
    variable_id variableId;
    std::variant<double, int, bool, std::string> value;
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
