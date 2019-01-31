#include <cse/algorithm.hpp>
#include <cse/manipulator.hpp>
#include <cse/scenario.hpp>

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

} // namespace


void scenario_manager::load_scenario(scenario::scenario s, time_point currentTime)
{
    scenario_ = s;
    remainingEvents_.clear();
    executedEvents_.clear();
    for (const auto& event : scenario_.events) {
        remainingEvents_[event.id] = event;
    }
    startTime_ = currentTime;
}

void scenario_manager::step_commencing(time_point currentTime)
{
    if (remainingEvents_.empty()) {
        return;
    }

    auto executedEvents = std::map<int, scenario::event>();
    for (const auto& [id, event] : remainingEvents_) {
        if (maybe_run_event(currentTime, event)) {
            executedEvents[id] = event;
        }
    }
    for (const auto& [id, event] : executedEvents) {
        remainingEvents_.erase(id);
        executedEvents_[id] = event;
    }
}

void scenario_manager::simulator_added(simulator_index index, simulator* sim, time_point /*currentTime*/)
{
    simulators_[index] = sim;
}

void scenario_manager::simulator_removed(simulator_index index, time_point)
{
    simulators_.erase(index);
}

scenario_manager::~scenario_manager()
{
}

void scenario_manager::execute_action(simulator* sim, const scenario::variable_action& a)
{
    std::visit(
        visitor(
            [=](double d) {
                auto f = [d](double /*original*/) { return d; };
                switch (a.causality) {
                    case variable_causality::input:
                    case variable_causality::parameter:
                        sim->expose_for_setting(variable_type::real, a.variable);
                        sim->set_real_input_manipulator(a.variable, f);
                        break;
                    case variable_causality::output:
                    case variable_causality::calculated_parameter:
                        sim->expose_for_getting(variable_type::real, a.variable);
                        sim->set_real_output_manipulator(a.variable, f);
                        break;
                    default:
                        throw std::invalid_argument("Cannot set real manipulator for this causality");
                }
            },
            [=](int i) {
                auto f = [i](int /*original*/) { return i; };
                switch (a.causality) {
                    case variable_causality::input:
                    case variable_causality::parameter:
                        sim->expose_for_setting(variable_type::integer, a.variable);
                        sim->set_integer_input_manipulator(a.variable, f);
                        break;
                    case variable_causality::output:
                    case variable_causality::calculated_parameter:
                        sim->expose_for_getting(variable_type::integer, a.variable);
                        sim->set_integer_output_manipulator(a.variable, f);
                        break;
                    default:
                        throw std::invalid_argument("Cannot set integer manipulator for this causality");
                }
            },
            [=](bool b) {
                auto f = [b](bool /*original*/) { return b; };
                switch (a.causality) {
                    case variable_causality::input:
                    case variable_causality::parameter:
                        sim->expose_for_setting(variable_type::boolean, a.variable);
                        sim->set_boolean_input_manipulator(a.variable, f);
                        break;
                    case variable_causality::output:
                    case variable_causality::calculated_parameter:
                        sim->expose_for_getting(variable_type::boolean, a.variable);
                        sim->set_boolean_output_manipulator(a.variable, f);
                        break;
                    default:
                        throw std::invalid_argument("Cannot set boolean manipulator for this causality");
                }
            },
            [=](std::string s) {
                auto f = [s](std::string_view /*original*/) { return s; };
                switch (a.causality) {
                    case variable_causality::input:
                    case variable_causality::parameter:
                        sim->expose_for_setting(variable_type::string, a.variable);
                        sim->set_string_input_manipulator(a.variable, f);
                        break;
                    case variable_causality::output:
                    case variable_causality::calculated_parameter:
                        sim->expose_for_getting(variable_type::string, a.variable);
                        sim->set_string_output_manipulator(a.variable, f);
                        break;
                    default:
                        throw std::invalid_argument("Cannot set string manipulator for this causality");
                }
            }),
        a.value);
}

bool scenario_manager::maybe_run_event(time_point currentTime, const scenario::event& e)
{
    if (currentTime >= e.trigger.trigger_point + startTime_.time_since_epoch()) {
        execute_action(simulators_[e.action.simulator], e.action);
        return true;
    }
    return false;
}

} // namespace cse
