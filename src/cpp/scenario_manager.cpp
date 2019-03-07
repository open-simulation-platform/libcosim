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

        for (const auto& event : s.events) {
            const auto index = static_cast<int>(state.remainingEvents.size());
            state.remainingEvents[index] = event;
        }
        state.running = true;

        BOOST_LOG_SEV(log::logger(), log::level::info) << "Successfully loaded scenario";
    }

    void load_scenario(const boost::filesystem::path& scenarioFile, time_point currentTime)
    {
        BOOST_LOG_SEV(log::logger(), log::level::info) << "Loading scenario from " << scenarioFile;
        auto scenario = parse_scenario(scenarioFile, simulators_);
        load_scenario(scenario, currentTime);
    }


    void step_commencing(time_point currentTime)
    {
        if (!state.running) {
            return;
        }

        const auto relativeTime = currentTime - state.startTime.time_since_epoch();

        bool timedOut = state.endTime ? *state.endTime > relativeTime : true;
        if (state.remainingEvents.empty() && timedOut) {
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

    void simulator_added(simulator_index index, simulator* sim)
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

    void execute_action(simulator* sim, const scenario::variable_action& a)
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

    bool maybe_run_event(time_point relativeTime, const scenario::event& e)
    {
        if (relativeTime >= e.time) {
            BOOST_LOG_SEV(log::logger(), log::level::info)
                << "Executing action for simulator " << e.action.simulator
                << ", variable " << e.action.variable
                << ", at relative time " << to_double_time_point(relativeTime);
            execute_action(simulators_[e.action.simulator], e.action);
            return true;
        }
        return false;
    }

    void cleanup_action(simulator* sim, const scenario::variable_action& a)
    {
        std::visit(
            visitor(
                [=](scenario::real_input_manipulator /*m*/) {
                    sim->set_real_input_manipulator(a.variable, nullptr);
                },
                [=](scenario::real_output_manipulator /*m*/) {
                    sim->set_real_output_manipulator(a.variable, nullptr);
                },
                [=](scenario::integer_input_manipulator /*m*/) {
                    sim->set_integer_input_manipulator(a.variable, nullptr);
                },
                [=](scenario::integer_output_manipulator /*m*/) {
                    sim->set_integer_output_manipulator(a.variable, nullptr);
                },
                [=](scenario::boolean_input_manipulator /*m*/) {
                    sim->set_boolean_input_manipulator(a.variable, nullptr);
                },
                [=](scenario::boolean_output_manipulator /*m*/) {
                    sim->set_boolean_output_manipulator(a.variable, nullptr);
                },
                [=](scenario::string_input_manipulator /*m*/) {
                    sim->set_string_input_manipulator(a.variable, nullptr);
                },
                [=](scenario::string_output_manipulator /*m*/) {
                    sim->set_string_output_manipulator(a.variable, nullptr);
                }),
            a.manipulator);
    }

    void cleanup(std::unordered_map<int, scenario::event> executedEvents)
    {
        for(const auto& entry : executedEvents) {
            auto e = entry.second;
            cleanup_action(simulators_[e.action.simulator], e.action);
        }
    }

    scenario_state state;
    std::unordered_map<simulator_index, simulator*> simulators_;
};

scenario_manager::~scenario_manager() noexcept = default;
scenario_manager::scenario_manager(scenario_manager&& other) noexcept = default;
scenario_manager& scenario_manager::operator=(scenario_manager&& other) noexcept = default;

scenario_manager::scenario_manager()
    : pimpl_(std::make_unique<impl>())
{
}

void scenario_manager::load_scenario(const scenario::scenario& s, time_point currentTime)
{
    pimpl_->load_scenario(s, currentTime);
}

void scenario_manager::load_scenario(const boost::filesystem::path& scenarioFile, time_point currentTime)
{
    pimpl_->load_scenario(scenarioFile, currentTime);
}

void scenario_manager::step_commencing(time_point currentTime)
{
    pimpl_->step_commencing(currentTime);
}

void scenario_manager::simulator_added(simulator_index index, simulator* sim, time_point /*currentTime*/)
{
    pimpl_->simulator_added(index, sim);
}

void scenario_manager::simulator_removed(simulator_index index, time_point)
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
