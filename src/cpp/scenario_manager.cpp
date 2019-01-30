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

void scenario_manager::simulator_added(simulator_index index, manipulable* sim, time_point /*currentTime*/)
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

void scenario_manager::execute_action(manipulable* sim, const scenario::variable_action& a)
{
    std::visit(
        visitor(
            [=](double d) {
                sim->expose_for_setting(a.variableId.type, a.variableId.index);
                sim->set_real_input_manipulator(a.variableId.index, [d](double /*original*/) { return d; });
            },
            [=](int i) {
                sim->expose_for_setting(a.variableId.type, a.variableId.index);
                sim->set_integer_input_manipulator(a.variableId.index, [i](int /*original*/) { return i; });
            },
            [=](bool b) {
                sim->expose_for_setting(a.variableId.type, a.variableId.index);
                sim->set_boolean_input_manipulator(a.variableId.index, [b](bool /*original*/) { return b; });
            },
            [=](std::string s) {
                sim->expose_for_setting(a.variableId.type, a.variableId.index);
                sim->set_string_input_manipulator(a.variableId.index, [s](std::string_view /*original*/) { return s; });
            }),
        a.value);
}

bool scenario_manager::maybe_run_event(time_point currentTime, const scenario::event& e)
{
    if (currentTime >= e.trigger.trigger_point + startTime_.time_since_epoch()) {
        execute_action(simulators_[e.action.variableId.simulator], e.action);
        return true;
    }
    return false;
}

} // namespace cse
