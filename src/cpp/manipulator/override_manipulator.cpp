
#include <cse/manipulator.hpp>
#include <cse/error.hpp>

#include <sstream>

namespace cse
{

namespace
{

template<typename... Functors>
struct visitor : Functors...
{
    visitor(const Functors&... functors)
        : Functors(functors)...
    {
    }

    using Functors::operator()...;
};

cse::variable_causality find_variable_causality(
    const std::vector<variable_description>& variables,
    const cse::variable_type type,
    const cse::value_reference reference)
{
    for (const auto& vd : variables) {
        if ((vd.reference == reference) && (vd.type == type)) {
            return vd.causality;
        }
    }
    std::ostringstream oss;
    oss << "Can't find variable with reference: " << reference
        << " and type: " << to_text(type);
    throw std::invalid_argument(oss.str());
}

bool is_input(cse::variable_causality causality)
{
    switch (causality) {
        case input:
        case parameter:
            return true;
        case calculated_parameter:
        case output:
            return false;
        default:
            std::ostringstream oss;
            oss << "No support for modifying a variable with causality: "
                << to_text(causality) << ".";
            throw std::invalid_argument(oss.str());
    }
}

} // namespace


void override_manipulator::simulator_added(
    simulator_index index,
    simulator* sim,
    time_point /*currentTime*/)
{
    simulators_[index] = sim;
}

void override_manipulator::simulator_removed(simulator_index index, time_point)
{
    simulators_.erase(index);
}

void override_manipulator::step_commencing(time_point /*currentTime*/)
{
    std::lock_guard<std::mutex> lock(lock_);
    if (!actions_.empty()) {
        for (const auto& a : actions_) {
            auto sim = simulators_.at(a.simulator);
            std::visit(
                visitor(
                    [=](scenario::real_modifier m) {
                        if (a.is_input) {
                            sim->expose_for_setting(variable_type::real, a.variable);
                            sim->set_real_input_modifier(a.variable, m.f);
                        } else {
                            sim->expose_for_getting(variable_type::real, a.variable);
                            sim->set_real_output_modifier(a.variable, m.f);
                        }
                    },
                    [=](scenario::integer_modifier m) {
                        if (a.is_input) {
                            sim->expose_for_setting(variable_type::integer, a.variable);
                            sim->set_integer_input_modifier(a.variable, m.f);
                        } else {
                            sim->expose_for_getting(variable_type::integer, a.variable);
                            sim->set_integer_output_modifier(a.variable, m.f);
                        }
                    },
                    [=](scenario::boolean_modifier m) {
                        if (a.is_input) {
                            sim->expose_for_setting(variable_type::boolean, a.variable);
                            sim->set_boolean_input_modifier(a.variable, m.f);
                        } else {
                            sim->expose_for_getting(variable_type::boolean, a.variable);
                            sim->set_boolean_output_modifier(a.variable, m.f);
                        }
                    },
                    [=](scenario::string_modifier m) {
                        if (a.is_input) {
                            sim->expose_for_setting(variable_type::string, a.variable);
                            sim->set_string_input_modifier(a.variable, m.f);
                        } else {
                            sim->expose_for_getting(variable_type::string, a.variable);
                            sim->set_string_output_modifier(a.variable, m.f);
                        }
                    }),
                a.modifier);
        }
        actions_.clear();
    }
}

void override_manipulator::add_action(
    simulator_index index,
    value_reference variable,
    variable_type type,
    const std::variant<
        scenario::real_modifier,
        scenario::integer_modifier,
        scenario::boolean_modifier,
        scenario::string_modifier>& m)
{
    auto sim = simulators_.at(index);
    auto causality = find_variable_causality(sim->model_description().variables, type, variable);
    bool input = is_input(causality);

    std::lock_guard<std::mutex> lock(lock_);
    actions_.emplace_back(scenario::variable_action{index, variable, m, input});
}

void override_manipulator::override_real_variable(
    simulator_index index,
    value_reference variable,
    double value)
{
    auto f = [value](double /*original*/) { return value; };
    add_action(index, variable, variable_type::real, scenario::real_modifier{f});
}

void override_manipulator::override_integer_variable(
    simulator_index index,
    value_reference variable,
    int value)
{
    auto f = [value](int /*original*/) { return value; };
    add_action(index, variable, variable_type::integer, scenario::integer_modifier{f});
}

void override_manipulator::override_boolean_variable(
    simulator_index index,
    value_reference variable,
    bool value)
{
    auto f = [value](bool /*original*/) { return value; };
    add_action(index, variable, variable_type::boolean, scenario::boolean_modifier{f});
}

void override_manipulator::override_string_variable(
    simulator_index index,
    value_reference variable,
    const std::string& value)
{
    auto f = [value](std::string_view /*original*/) { return value; };
    add_action(index, variable, variable_type::string, scenario::string_modifier{f});
}

void override_manipulator::reset_variable(
    simulator_index index,
    variable_type type,
    value_reference variable)
{
    switch (type) {
        case variable_type::real:
            add_action(index, variable, variable_type::real, scenario::real_modifier{nullptr});
            break;
        case variable_type::integer:
            add_action(index, variable, variable_type::integer, scenario::integer_modifier{nullptr});
            break;
        case variable_type::boolean:
            add_action(index, variable, variable_type::boolean, scenario::boolean_modifier{nullptr});
            break;
        case variable_type::string:
            add_action(index, variable, variable_type::string, scenario::string_modifier{nullptr});
            break;
        default:
            CSE_PANIC();
    }
}

override_manipulator::~override_manipulator() = default;

} // namespace cse
