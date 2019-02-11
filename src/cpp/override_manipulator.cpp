#include "cse/manipulator.hpp"

namespace cse
{

namespace
{

cse::variable_causality find_variable_causality(const std::vector<variable_description>& variables,
    const cse::variable_type type,
    const cse::variable_index index)
{
    for (const auto& vd : variables) {
        if ((vd.index == index) && (vd.type == type)) {
            return vd.causality;
        }
    }
    throw std::invalid_argument("Can't find variable causality");
}

} // namespace

void override_manipulator::simulator_added(simulator_index index, simulator* sim, time_point /*currentTime*/)
{
    simulators_[index] = sim;
}

void override_manipulator::simulator_removed(simulator_index index, time_point)
{
    simulators_.erase(index);
}

void override_manipulator::step_commencing(time_point /*currentTime*/)
{
}

void override_manipulator::override_real_variable(simulator_index index, variable_index variable, double value)
{
    auto f = [value](double /*original*/) { return value; };
    auto sim = simulators_.at(index);
    auto causality = find_variable_causality(sim->model_description().variables, variable_type::real, variable);
    switch (causality) {
        case input:
        case parameter:
            sim->expose_for_setting(variable_type::real, variable);
            sim->set_real_input_manipulator(variable, f);
            break;
        case calculated_parameter:
        case output:
            sim->expose_for_getting(variable_type::real, variable);
            sim->set_real_output_manipulator(variable, f);
            break;
        default:
            throw std::invalid_argument("No support for manipulating a variable with this causality");
    }
}

void override_manipulator::override_integer_variable(simulator_index index, variable_index variable, int value)
{
    auto f = [value](int /*original*/) { return value; };
    auto sim = simulators_.at(index);
    auto causality = find_variable_causality(sim->model_description().variables, variable_type::integer, variable);
    switch (causality) {
        case input:
        case parameter:
            sim->expose_for_setting(variable_type::integer, variable);
            sim->set_integer_input_manipulator(variable, f);
            break;
        case calculated_parameter:
        case output:
            sim->expose_for_getting(variable_type::integer, variable);
            sim->set_integer_output_manipulator(variable, f);
            break;
        default:
            throw std::invalid_argument("No support for manipulating a variable with this causality");
    }
}
void override_manipulator::override_boolean_variable(simulator_index index, variable_index variable, bool value)
{
    auto f = [value](bool /*original*/) { return value; };
    auto sim = simulators_.at(index);
    auto causality = find_variable_causality(sim->model_description().variables, variable_type::boolean, variable);
    switch (causality) {
        case input:
        case parameter:
            sim->expose_for_setting(variable_type::boolean, variable);
            sim->set_boolean_input_manipulator(variable, f);
            break;
        case calculated_parameter:
        case output:
            sim->expose_for_getting(variable_type::boolean, variable);
            sim->set_boolean_output_manipulator(variable, f);
            break;
        default:
            throw std::invalid_argument("No support for manipulating a variable with this causality");
    }
}
void override_manipulator::override_string_variable(simulator_index index, variable_index variable, std::string value)
{
    auto f = [value](std::string_view /*original*/) { return value; };
    auto sim = simulators_.at(index);
    auto causality = find_variable_causality(sim->model_description().variables, variable_type::string, variable);
    switch (causality) {
        case input:
        case parameter:
            sim->expose_for_setting(variable_type::string, variable);
            sim->set_string_input_manipulator(variable, f);
            break;
        case calculated_parameter:
        case output:
            sim->expose_for_getting(variable_type::string, variable);
            sim->set_string_output_manipulator(variable, f);
            break;
        default:
            throw std::invalid_argument("No support for manipulating a variable with this causality");
    }
}

override_manipulator::~override_manipulator() = default;

} // namespace cse