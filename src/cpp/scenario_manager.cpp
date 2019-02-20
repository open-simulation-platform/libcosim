#include "cse/algorithm.hpp"
#include "cse/log.hpp"
#include "cse/log/logger.hpp"
#include "cse/manipulator.hpp"
#include "cse/scenario.hpp"
#include "cse/scenario_parser.hpp"

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
        const auto index = static_cast<int>(remainingEvents_.size());
        remainingEvents_[index] = event;
    }
    startTime_ = currentTime;
}

void scenario_manager::load_scenario(boost::filesystem::path scenarioFile, time_point currentTime)
{
    auto scenario = parse_scenario(scenarioFile, simulators_);
    load_scenario(scenario, currentTime);
}


void scenario_manager::step_commencing(time_point currentTime)
{
    if (remainingEvents_.empty()) {
        return;
    }

    auto executedEvents = std::map<int, scenario::event>();
    for (const auto& [index, event] : remainingEvents_) {
        if (maybe_run_event(currentTime, event)) {
            executedEvents[index] = event;
        }
    }
    for (const auto& [index, event] : executedEvents) {
        remainingEvents_.erase(index);
        executedEvents_[index] = event;
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
            [=](scenario::real_input_manipulator m) {
                sim->expose_for_setting(variable_type::real, a.variable);
                sim->set_real_input_manipulator(a.variable, m.f);
            },
            [=](scenario::real_output_manipulator m) {
                sim->expose_for_getting(variable_type::real, a.variable);
                sim->set_real_output_manipulator(a.variable, m.f);
            },
            [=](scenario::integer_input_manipulator m) {
                sim->expose_for_setting(variable_type::integer, a.variable);
                sim->set_integer_input_manipulator(a.variable, m.f);
            },
            [=](scenario::integer_output_manipulator m) {
                sim->expose_for_getting(variable_type::integer, a.variable);
                sim->set_integer_output_manipulator(a.variable, m.f);
            },
            [=](scenario::boolean_input_manipulator m) {
                sim->expose_for_setting(variable_type::boolean, a.variable);
                sim->set_boolean_input_manipulator(a.variable, m.f);
            },
            [=](scenario::boolean_output_manipulator m) {
                sim->expose_for_getting(variable_type::boolean, a.variable);
                sim->set_boolean_output_manipulator(a.variable, m.f);
            },
            [=](scenario::string_input_manipulator m) {
                sim->expose_for_setting(variable_type::string, a.variable);
                sim->set_string_input_manipulator(a.variable, m.f);
            },
            [=](scenario::string_output_manipulator m) {
                sim->expose_for_getting(variable_type::string, a.variable);
                sim->set_string_output_manipulator(a.variable, m.f);
            }),
        a.manipulator);
}

bool scenario_manager::maybe_run_event(time_point currentTime, const scenario::event& e)
{
    if (currentTime >= e.time + startTime_.time_since_epoch()) {
        execute_action(simulators_[e.action.simulator], e.action);
        return true;
    }
    return false;
}

} // namespace cse
