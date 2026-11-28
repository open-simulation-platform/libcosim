/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/manipulator/scenario_manager.hpp"

#include "cosim/execution.hpp"
#include "cosim/log/logger.hpp"
#include "cosim/manipulator/manipulator.hpp"
#include "cosim/model_description.hpp"
#include "cosim/scenario.hpp"
#include "cosim/scenario_parser.hpp"
#include "cosim/time.hpp"
#include "cosim/utility/utility.hpp"

#include <boost/log/sources/severity_feature.hpp>

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>


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

    void load_scenario(const scenario::scenario& scenarioData, time_point currentTime)
    {
        for (const auto& event : scenarioData.events) {
            find_simulator(event.action.simulator);
        }
        scenario_state next;
        next.startTime = currentTime;
        next.endTime = scenarioData.end;

        for (const auto& event : scenarioData.events) {
            const auto index = static_cast<int>(next.remainingEvents.size());
            next.remainingEvents[index] = event;
        }
        next.running = true;
        cleanup(state.executedEvents);
        state = std::move(next);

        BOOST_LOG_SEV(log::logger(), log::info) << "Successfully loaded scenario";
    }

    void load_scenario(const cosim::filesystem::path& scenarioFile, time_point currentTime)
    {
        BOOST_LOG_SEV(log::logger(), log::info) << "Loading scenario from " << scenarioFile;
        const auto scenario = parse_scenario(scenarioFile, simulators_);
        load_scenario(scenario, currentTime);
    }


    void step_commencing(time_point currentTime)
    {
        if (!state.running) {
            return;
        }

        const auto relativeTime = currentTime - state.startTime.time_since_epoch();

        bool const timedOut = state.endTime ? relativeTime >= *state.endTime : true;
        if (state.remainingEvents.empty() && timedOut) {
            BOOST_LOG_SEV(log::logger(), log::info)
                << "Scenario finished at relative time "
                << to_double_time_point(relativeTime)
                << ", performing cleanup";
            state.running = false;
            cleanup(state.executedEvents);
            state.executedEvents.clear();
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

    [[nodiscard]] bool is_scenario_running() const
    {
        return state.running;
    }

    void abort_scenario()
    {
        BOOST_LOG_SEV(log::logger(), log::info)
            << "Scenario aborted, performing cleanup";
        state.running = false;
        cleanup(state.executedEvents);
        state.remainingEvents.clear();
        state.executedEvents.clear();
    }

private:
    manipulable* find_simulator(simulator_index index) const // NOLINT(modernize-use-nodiscard)
    {
        const auto found = simulators_.find(index);
        if (found == simulators_.end() || (found->second == nullptr)) {
            throw std::invalid_argument(
                "Scenario references an unknown simulator: " + std::to_string(index));
        }
        return found->second;
    }

    struct scenario_state
    {
        std::unordered_map<int, scenario::event> remainingEvents;
        std::unordered_map<int, scenario::event> executedEvents;
        time_point startTime;
        std::optional<time_point> endTime;
        bool running = false;
    };

    static void execute_action(manipulable* sim, const scenario::variable_action& action)
    {
        // Keep the modifier copies: std::function targets may have observable copy behavior.
        // NOLINTBEGIN(performance-unnecessary-value-param)
        std::visit(
            visitor(
                [=](scenario::real_modifier modifier) {
                    if (action.is_input) {
                        sim->expose_for_setting(variable_type::real, action.variable);
                        sim->set_real_input_modifier(action.variable, modifier.f);
                    } else {
                        sim->expose_for_getting(variable_type::real, action.variable);
                        sim->set_real_output_modifier(action.variable, modifier.f);
                    }
                },
                [=](scenario::integer_modifier modifier) {
                    if (action.is_input) {
                        sim->expose_for_setting(variable_type::integer, action.variable);
                        sim->set_integer_input_modifier(action.variable, modifier.f);
                    } else {
                        sim->expose_for_getting(variable_type::integer, action.variable);
                        sim->set_integer_output_modifier(action.variable, modifier.f);
                    }
                },
                [=](scenario::boolean_modifier modifier) {
                    if (action.is_input) {
                        sim->expose_for_setting(variable_type::boolean, action.variable);
                        sim->set_boolean_input_modifier(action.variable, modifier.f);
                    } else {
                        sim->expose_for_getting(variable_type::boolean, action.variable);
                        sim->set_boolean_output_modifier(action.variable, modifier.f);
                    }
                },
                [=](scenario::string_modifier modifier) {
                    if (action.is_input) {
                        sim->expose_for_setting(variable_type::string, action.variable);
                        sim->set_string_input_modifier(action.variable, modifier.f);
                    } else {
                        sim->expose_for_getting(variable_type::string, action.variable);
                        sim->set_string_output_modifier(action.variable, modifier.f);
                    }
                },
                [=](scenario::variable_modifier modifier) {
                    if (action.is_input) {
                        sim->expose_for_setting(modifier.type, action.variable);
                        sim->set_variable_input_modifier(
                            modifier.type, action.variable, modifier.f);
                    } else {
                        sim->expose_for_getting(modifier.type, action.variable);
                        sim->set_variable_output_modifier(
                            modifier.type, action.variable, modifier.f);
                    }
                }),
            action.modifier);
        // NOLINTEND(performance-unnecessary-value-param)
    }

    bool maybe_run_event(time_point relativeTime, const scenario::event& event)
    {
        if (relativeTime >= event.time) {
            BOOST_LOG_SEV(log::logger(), log::info)
                << "Executing action for simulator " << event.action.simulator
                << ", variable " << event.action.variable
                << ", at relative time " << to_double_time_point(relativeTime);
            execute_action(find_simulator(event.action.simulator), event.action);
            return true;
        }
        return false;
    }

    static void cleanup_action(manipulable* sim, const scenario::variable_action& action)
    {
        BOOST_LOG_SEV(log::logger(), log::info)
            << "Resetting variable for simulator " << action.simulator
            << ", variable " << action.variable;
        // Keep the modifier copies: std::function targets may have observable copy behavior.
        // NOLINTBEGIN(performance-unnecessary-value-param)
        std::visit(
            visitor(
                [=](scenario::real_modifier /*m*/) {
                    if (action.is_input) {
                        sim->set_real_input_modifier(action.variable, nullptr);
                    } else {
                        sim->set_real_output_modifier(action.variable, nullptr);
                    }
                },
                [=](scenario::integer_modifier /*m*/) {
                    if (action.is_input) {
                        sim->set_integer_input_modifier(action.variable, nullptr);
                    } else {
                        sim->set_integer_output_modifier(action.variable, nullptr);
                    }
                },
                [=](scenario::boolean_modifier /*m*/) {
                    if (action.is_input) {
                        sim->set_boolean_input_modifier(action.variable, nullptr);
                    } else {
                        sim->set_boolean_output_modifier(action.variable, nullptr);
                    }
                },
                [=](scenario::string_modifier /*m*/) {
                    if (action.is_input) {
                        sim->set_string_input_modifier(action.variable, nullptr);
                    } else {
                        sim->set_string_output_modifier(action.variable, nullptr);
                    }
                },
                [=](scenario::variable_modifier modifier) {
                    if (action.is_input) {
                        sim->set_variable_input_modifier(
                            modifier.type, action.variable, nullptr);
                    } else {
                        sim->set_variable_output_modifier(
                            modifier.type, action.variable, nullptr);
                    }
                }),
            action.modifier);
        // NOLINTEND(performance-unnecessary-value-param)
    }

    void cleanup(const std::unordered_map<int, scenario::event>& executedEvents)
    {
        for (const auto& entry : executedEvents) {
            const auto event = entry.second;
            cleanup_action(find_simulator(event.action.simulator), event.action);
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
    const scenario::scenario& scenarioData,
    time_point currentTime)
{
    pimpl_->load_scenario(scenarioData, currentTime);
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
    time_point /*unused*/)
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
