#include "../../include/cse/scenario.hpp"

#include "cse/manipulator.hpp"

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
    std::lock_guard<std::mutex> lock(lock_);
    if (!actions_.empty()) {
        for (const auto& action : actions_) {
            auto sim = simulators_.at(action.simulator);
            std::visit(
                visitor(
                    [=](scenario::real_input_manipulator m) {
                        sim->expose_for_setting(variable_type::real, action.variable);
                        sim->set_real_input_manipulator(action.variable, m.f);
                    },
                    [=](scenario::real_output_manipulator m) {
                        sim->expose_for_getting(variable_type::real, action.variable);
                        sim->set_real_output_manipulator(action.variable, m.f);
                    },
                    [=](scenario::integer_input_manipulator m) {
                        sim->expose_for_setting(variable_type::integer, action.variable);
                        sim->set_integer_input_manipulator(action.variable, m.f);
                    },
                    [=](scenario::integer_output_manipulator m) {
                        sim->expose_for_getting(variable_type::integer, action.variable);
                        sim->set_integer_output_manipulator(action.variable, m.f);
                    },
                    [=](scenario::boolean_input_manipulator m) {
                        sim->expose_for_setting(variable_type::boolean, action.variable);
                        sim->set_boolean_input_manipulator(action.variable, m.f);
                    },
                    [=](scenario::boolean_output_manipulator m) {
                        sim->expose_for_getting(variable_type::boolean, action.variable);
                        sim->set_boolean_output_manipulator(action.variable, m.f);
                    },
                    [=](scenario::string_input_manipulator m) {
                        sim->expose_for_setting(variable_type::string, action.variable);
                        sim->set_string_input_manipulator(action.variable, m.f);
                    },
                    [=](scenario::string_output_manipulator m) {
                        sim->expose_for_getting(variable_type::string, action.variable);
                        sim->set_string_output_manipulator(action.variable, m.f);
                    }),
                action.manipulator);
        }
        actions_.clear();
    }
}


template<typename I, typename O, typename M, typename N>
void override_manipulator::add_action(simulator_index index, variable_index variable, variable_type type, std::function<O(I)> f)
{
    auto sim = simulators_.at(index);
    auto causality = find_variable_causality(sim->model_description().variables, type, variable);
    std::lock_guard<std::mutex> lock(lock_);
    switch (causality) {
        case input:
        case parameter:
            actions_.emplace_back(scenario::variable_action{index, variable, M{f}});
            break;
        case calculated_parameter:
        case output:
            actions_.emplace_back(scenario::variable_action{index, variable, N{f}});
            break;
        default:
            throw std::invalid_argument("No support for manipulating a variable with this causality");
    }
}

void override_manipulator::override_real_variable(simulator_index index, variable_index variable, double value)
{
    auto f = [value](double /*original*/) { return value; };
    add_action<double, double, scenario::real_input_manipulator, scenario::real_output_manipulator>(index, variable, cse::variable_type::real, f);
}

void override_manipulator::override_integer_variable(simulator_index index, variable_index variable, int value)
{
    auto f = [value](int /*original*/) { return value; };
    add_action<int, int, scenario::integer_input_manipulator, scenario::integer_output_manipulator>(index, variable, cse::variable_type::integer, f);
}

void override_manipulator::override_boolean_variable(simulator_index index, variable_index variable, bool value)
{
    auto f = [value](bool /*original*/) { return value; };
    add_action<bool, bool, scenario::boolean_input_manipulator, scenario::boolean_output_manipulator>(index, variable, cse::variable_type::boolean, f);
}

void override_manipulator::override_string_variable(simulator_index index, variable_index variable, const std::string& value)
{
    auto f = [value](std::string_view /*original*/) { return value; };
    add_action<std::string_view, std::string, scenario::string_input_manipulator, scenario::string_output_manipulator>(index, variable, cse::variable_type::string, f);
}

void override_manipulator::reset_real_variable(simulator_index index, variable_index variable)
{
    add_action<double, double, scenario::real_input_manipulator, scenario::real_output_manipulator>(index, variable, cse::variable_type::real, nullptr);
}

void override_manipulator::reset_integer_variable(simulator_index index, variable_index variable)
{
    add_action<int, int, scenario::integer_input_manipulator, scenario::integer_output_manipulator>(index, variable, cse::variable_type::integer, nullptr);
}

void override_manipulator::reset_boolean_variable(simulator_index index, variable_index variable)
{
    add_action<bool, bool, scenario::boolean_input_manipulator, scenario::boolean_output_manipulator>(index, variable, cse::variable_type::boolean, nullptr);
}

void override_manipulator::reset_string_variable(simulator_index index, variable_index variable)
{
    add_action<std::string_view, std::string, scenario::string_input_manipulator, scenario::string_output_manipulator>(index, variable, cse::variable_type::string, nullptr);
}

override_manipulator::~override_manipulator() = default;

} // namespace cse
