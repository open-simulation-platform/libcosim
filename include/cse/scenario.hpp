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

/// The modification of the value of a variable with type `real`.
struct real_modifier
{
    /// A function which may be called any number of times. Can be `nullptr`.
    std::function<double(double)> f;
};

/// The modification of the value of a variable with type `integer`.
struct integer_modifier
{
    /// A function which may be called any number of times. Can be `nullptr`.
    std::function<int(int)> f;
};

/// The modification of the value of a variable with type `boolean`.
struct boolean_modifier
{
    /// A function which may be called any number of times. Can be `nullptr`.
    std::function<bool(bool)> f;
};

/// The modification of the value of a variable with type `string`.
struct string_modifier
{
    /// A function which may be called any number of times. Can be `nullptr`.
    std::function<std::string(std::string_view)> f;
};

/// A struct specifying a variable and the modification of its value.
struct variable_action
{
    /// The simulator index.
    simulator_index simulator;
    /// The variable value reference.
    value_reference variable;
    /// The modification to be done to the variable's value.
    std::variant<
        real_modifier,
        integer_modifier,
        boolean_modifier,
        string_modifier>
        modifier;
    /**
     * Flag which should be set to `true` if the variable is an *input* to the
     * slave (i.e. causality input or parameter), or `false` if the variable is
     * an *output* from a slave (i.e. causality output or calculatedParameter).
     */
    bool is_input;
};

/// A struct representing an event.
struct event
{
    /// The time point at which the event should trigger.
    time_point time;
    /// Something which should happen to a variable.
    variable_action action;
};

/// A struct representing an executable scenario.
struct scenario
{
    /// A collection of time-based events.
    std::vector<event> events;
    /// An optional time point at which the scenario should terminate.
    std::optional<time_point> end;
};

} // namespace scenario

} // namespace cse

#endif //CSECORE_SCENARIO_HPP
