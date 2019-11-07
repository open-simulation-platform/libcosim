#include "cse/manipulator/scenario_manager.hpp"

#include "cse/algorithm.hpp"
#include "cse/log/logger.hpp"
#include "cse/scenario.hpp"
#include "cse/scenario_parser.hpp"
#include "cse/utility/utility.hpp"

namespace cse
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

        BOOST_LOG_SEV(log::logger(), log::info) << "Successfully loaded scenario";
    }

    void load_scenario(const boost::filesystem::path& scenarioFile, time_point currentTime)
    {
        BOOST_LOG_SEV(log::logger(), log::info) << "Loading scenario from " << scenarioFile;
        auto scenario = parse_scenario(scenarioFile, simulators_);
        load_scenario(scenario, currentTime);
    }


    void step_commencing(time_point currentTime)
    {
        if (!state.running) {
            return;
        }

        const auto relativeTime = currentTime - state.startTime.time_since_epoch();

        if (state.endTime && (relativeTime >= *state.endTime)) {
            BOOST_LOG_SEV(log::logger(), log::info)
                << "Scenario finished at relative time "
                << to_double_time_point(relativeTime)
                << ", performing cleanup";
            state.running = false;
            cleanup();
            return;
        }

        auto executedEvents = std::map<int, scenario::event>();

        for (auto& [index, event] : state.remainingEvents) {
            if (event_execution_complete(relativeTime, event)) {
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
        BOOST_LOG_SEV(log::logger(), log::info)
            << "Scenario aborted, performing cleanup";
        state.running = false;
        cleanup();
        state.remainingEvents.clear();
        state.executedEvents.clear();
    }

private:
    struct scenario_state
    {
        std::unordered_map<int, scenario::event> remainingEvents;
        std::unordered_map<int, scenario::event> executingEvents;
        std::unordered_map<int, scenario::event> executedEvents;
        time_point startTime;
        std::optional<time_point> endTime;
        bool running = false;
    };

    void execute_action(manipulable* sim, scenario::variable_action& a, time_point eventTime, const bool& shouldReset)
    {
        std::visit(
            visitor(
                [=](const scenario::real_modifier& m) {
                    if (a.is_input) {
                        sim->expose_for_setting(variable_type::real, a.variable);
                        sim->set_real_input_modifier(a.variable, m.f);
                    } else {
                        sim->expose_for_getting(variable_type::real, a.variable);
                        sim->set_real_output_modifier(a.variable, m.f);
                    }
                },
                [=](scenario::time_dependent_real_modifier& m) {
                    const auto& orgFn = m.f;
                    const auto& newFn = shouldReset ? std::function<double(double)>(nullptr) : [orgFn, eventTime](double d) { return orgFn(d, eventTime); };

                    if (a.is_input) {
                        sim->expose_for_setting(variable_type::real, a.variable);
                        sim->set_real_input_modifier(a.variable, newFn);
                    } else {
                        sim->expose_for_getting(variable_type::real, a.variable);
                        sim->set_real_output_modifier(a.variable, newFn);
                    }
                },
                [=](const scenario::integer_modifier& m) {
                    if (a.is_input) {
                        sim->expose_for_setting(variable_type::integer, a.variable);
                        sim->set_integer_input_modifier(a.variable, m.f);
                    } else {
                        sim->expose_for_getting(variable_type::integer, a.variable);
                        sim->set_integer_output_modifier(a.variable, m.f);
                    }
                },
                [=](const scenario::time_dependent_integer_modifier& m) {
                    const auto& orgFn = m.f;
                    const auto& newFn = shouldReset ? std::function<int(int)>(nullptr) : [orgFn, eventTime](int i) { return orgFn(i, eventTime); };

                    if (a.is_input) {
                        sim->expose_for_setting(variable_type::integer, a.variable);
                        sim->set_integer_input_modifier(a.variable, newFn);
                    } else {
                        sim->expose_for_getting(variable_type::integer, a.variable);
                        sim->set_integer_output_modifier(a.variable, newFn);
                    }
                },
                [=](const scenario::boolean_modifier& m) {
                    if (a.is_input) {
                        sim->expose_for_setting(variable_type::boolean, a.variable);
                        sim->set_boolean_input_modifier(a.variable, m.f);
                    } else {
                        sim->expose_for_getting(variable_type::boolean, a.variable);
                        sim->set_boolean_output_modifier(a.variable, m.f);
                    }
                },
                [=](const scenario::string_modifier& m) {
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

    void execute_event(time_point relativeTime, scenario::event& e, const bool& shouldReset)
    {
        BOOST_LOG_SEV(log::logger(), log::info)
            << "Executing action for simulator " << e.action.simulator
            << ", variable " << e.action.variable
            << ", at relative time " << to_double_time_point(relativeTime);

        const auto eventTime = time_point(relativeTime - e.time);
        execute_action(simulators_[e.action.simulator], e.action, eventTime, shouldReset);
    }

    bool event_execution_complete(time_point relativeTime, scenario::event& e)
    {
        if (e.resetTime.has_value() && relativeTime >= e.resetTime) {
            execute_event(relativeTime, e, true);
            return true;
        }

        if (relativeTime >= e.time) {
            execute_event(relativeTime, e, false);
            return !e.action.is_time_dependent;
        }

        return false;
    }

    void cleanup_action(manipulable* sim, const scenario::variable_action& a)
    {
        BOOST_LOG_SEV(log::logger(), log::info)
            << "Resetting variable for simulator " << a.simulator
            << ", variable " << a.variable;
        std::visit(
            visitor(
                [=](const scenario::real_modifier& /*m*/) {
                    if (a.is_input) {
                        sim->set_real_input_modifier(a.variable, nullptr);
                    } else {
                        sim->set_real_output_modifier(a.variable, nullptr);
                    }
                },
                [=](const scenario::time_dependent_real_modifier& /*m*/) {
                    if (a.is_input) {
                        sim->set_real_input_modifier(a.variable, nullptr);
                    } else {
                        sim->set_real_output_modifier(a.variable, nullptr);
                    }
                },
                [=](const scenario::integer_modifier& /*m*/) {
                    if (a.is_input) {
                        sim->set_integer_input_modifier(a.variable, nullptr);
                    } else {
                        sim->set_integer_output_modifier(a.variable, nullptr);
                    }
                },
                [=](const scenario::time_dependent_integer_modifier& /*m*/) {
                    if (a.is_input) {
                        sim->set_integer_input_modifier(a.variable, nullptr);
                    } else {
                        sim->set_integer_output_modifier(a.variable, nullptr);
                    }
                },
                [=](const scenario::boolean_modifier& /*m*/) {
                    if (a.is_input) {
                        sim->set_boolean_input_modifier(a.variable, nullptr);
                    } else {
                        sim->set_boolean_output_modifier(a.variable, nullptr);
                    }
                },
                [=](const scenario::string_modifier& /*m*/) {
                    if (a.is_input) {
                        sim->set_string_input_modifier(a.variable, nullptr);
                    } else {
                        sim->set_string_output_modifier(a.variable, nullptr);
                    }
                }),
            a.modifier);
    }

    void cleanup()
    {
        for (const auto& entry : state.executedEvents) {
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
    const boost::filesystem::path& scenarioFile,
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

} // namespace cse
