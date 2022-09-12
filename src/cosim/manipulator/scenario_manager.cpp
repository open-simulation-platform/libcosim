/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/manipulator/scenario_manager.hpp"

#include "cosim/algorithm.hpp"
#include "cosim/log/logger.hpp"
#include "cosim/scenario.hpp"
#include "cosim/scenario_parser.hpp"
#include "cosim/utility/utility.hpp"


namespace cosim
{

class scenario_manager::impl
{

public:
    impl() = default;
    ~impl() noexcept = default;

    impl(const impl&) = delete;
    impl& operator=(const impl&) = delete;

    impl(impl&&) = delete;
    impl& operator=(impl&&) = delete;

    void load_scenario(const scenario::scenario& s, time_point currentTime)
    {
        state = scenario_state{};
        state.startTime = currentTime;
        state.endTime = s.end;

        for (const auto& event : s.events) {
            const auto index = static_cast<int>(state.remainingEvents.size());
            state.remainingEvents[index] = event;
        }
        state.running = true;

        log::info("Successfully loaded scenario");
    }

    void load_scenario(const cosim::filesystem::path& scenarioFile, time_point currentTime)
    {
        log::info("Loading scenario from {}", scenarioFile.string());
        auto scenario = parse_scenario(scenarioFile, simulators_);
        load_scenario(scenario, currentTime);
    }


    void step_commencing(time_point currentTime)
    {
        if (!state.running) {
            return;
        }

        const auto relativeTime = currentTime - state.startTime.time_since_epoch();

        bool timedOut = state.endTime ? relativeTime >= *state.endTime : true;
        if (state.remainingEvents.empty() && timedOut) {
            log::info("Scenario finished at relative time {}, performing cleanup", to_double_time_point(relativeTime));
            state.running = false;
            cleanup(state.executedEvents);
            return;
        }

        auto executedEvents = std::map<int, scenario::event>();
        for (const auto& [index, event] : state.remainingEvents) {
            if (maybe_run_event(relativeTime, event)) {
                executedEvents[index] = event;
            }
        }
        for (const auto& [index, event] : executedEvents) {
            state.remainingEvents.erase(index);
            state.executedEvents[index] = event;
        }
    }

    void simulator_added(simulator_index index, manipulable* sim)
    {
        simulators_[index] = sim;
    }

    void simulator_removed(simulator_index index)
    {
        simulators_.erase(index);
    }

    bool is_scenario_running()
    {
        return state.running;
    }

    void abort_scenario()
    {
        log::info("Scenario aborted, performing cleanup");
        state.running = false;
        cleanup(state.executedEvents);
        state.remainingEvents.clear();
        state.executedEvents.clear();
    }

private:
    struct scenario_state
    {
        std::unordered_map<int, scenario::event> remainingEvents;
        std::unordered_map<int, scenario::event> executedEvents;
        time_point startTime;
        std::optional<time_point> endTime;
        bool running = false;
    };

    void execute_action(manipulable* sim, const scenario::variable_action& a)
    {
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

    bool maybe_run_event(time_point relativeTime, const scenario::event& e)
    {
        if (relativeTime >= e.time) {
            log::info("Executing action for simulator {}, variable {}, at relative time {}",
                e.action.simulator, e.action.variable, to_double_time_point(relativeTime));
            execute_action(simulators_[e.action.simulator], e.action);
            return true;
        }
        return false;
    }

    void cleanup_action(manipulable* sim, const scenario::variable_action& a)
    {
        log::info("Resetting variable for simulator {}, variable {}", a.simulator, a.variable);
        std::visit(
            visitor(
                [=](scenario::real_modifier /*m*/) {
                    if (a.is_input) {
                        sim->set_real_input_modifier(a.variable, nullptr);
                    } else {
                        sim->set_real_output_modifier(a.variable, nullptr);
                    }
                },
                [=](scenario::integer_modifier /*m*/) {
                    if (a.is_input) {
                        sim->set_integer_input_modifier(a.variable, nullptr);
                    } else {
                        sim->set_integer_output_modifier(a.variable, nullptr);
                    }
                },
                [=](scenario::boolean_modifier /*m*/) {
                    if (a.is_input) {
                        sim->set_boolean_input_modifier(a.variable, nullptr);
                    } else {
                        sim->set_boolean_output_modifier(a.variable, nullptr);
                    }
                },
                [=](scenario::string_modifier /*m*/) {
                    if (a.is_input) {
                        sim->set_string_input_modifier(a.variable, nullptr);
                    } else {
                        sim->set_string_output_modifier(a.variable, nullptr);
                    }
                }),
            a.modifier);
    }

    void cleanup(const std::unordered_map<int, scenario::event>& executedEvents)
    {
        for (const auto& entry : executedEvents) {
            auto e = entry.second;
            cleanup_action(simulators_[e.action.simulator], e.action);
        }
    }

    scenario_state state;
    std::unordered_map<simulator_index, manipulable*> simulators_;
};

scenario_manager::~scenario_manager() noexcept = default;
scenario_manager::scenario_manager(scenario_manager&& other) noexcept = default;
scenario_manager& scenario_manager::operator=(scenario_manager&& other) noexcept = default;

scenario_manager::scenario_manager()
    : pimpl_(std::make_unique<impl>())
{
}

void scenario_manager::load_scenario(
    const scenario::scenario& s,
    time_point currentTime)
{
    pimpl_->load_scenario(s, currentTime);
}

void scenario_manager::load_scenario(
    const cosim::filesystem::path& scenarioFile,
    time_point currentTime)
{
    pimpl_->load_scenario(scenarioFile, currentTime);
}

void scenario_manager::step_commencing(
    time_point currentTime)
{
    pimpl_->step_commencing(currentTime);
}

void scenario_manager::simulator_added(
    simulator_index index,
    manipulable* sim,
    time_point /*currentTime*/)
{
    pimpl_->simulator_added(index, sim);
}

void scenario_manager::simulator_removed(
    simulator_index index,
    time_point)
{
    pimpl_->simulator_removed(index);
}

bool scenario_manager::is_scenario_running()
{
    return pimpl_->is_scenario_running();
}

void scenario_manager::abort_scenario()
{
    pimpl_->abort_scenario();
}

} // namespace cosim
