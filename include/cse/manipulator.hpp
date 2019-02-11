#ifndef CSECORE_MANIPULATOR_H
#define CSECORE_MANIPULATOR_H

#include <cse/algorithm.hpp>
#include <cse/model.hpp>
#include <cse/scenario.hpp>

#include <map>
#include <string>
#include <unordered_map>

#include <boost/filesystem/path.hpp>

namespace cse
{

class manipulator
{
public:
    /// A simulator was added to the execution.
    virtual void simulator_added(simulator_index, simulator*, time_point) = 0;

    /// A simulator was removed from the execution.
    virtual void simulator_removed(simulator_index, time_point) = 0;

    /// A time step is commencing.
    virtual void step_commencing(
        time_point currentTime) = 0;

    virtual ~manipulator() noexcept {}
};

class scenario_manager : public manipulator
{
public:
    void simulator_added(simulator_index, simulator*, time_point) override;

    void simulator_removed(simulator_index, time_point) override;

    void step_commencing(
        time_point currentTime) override;

    void load_scenario(scenario::scenario s, time_point currentTime);

    void load_scenario(boost::filesystem::path scenarioFile, time_point currentTime);

    ~scenario_manager() noexcept override;

private:
    std::map<int, scenario::event> remainingEvents_;
    std::map<int, scenario::event> executedEvents_;

    scenario::scenario scenario_;
    time_point startTime_;
    std::unordered_map<simulator_index, simulator*> simulators_;

    void execute_action(simulator* sim, const scenario::variable_action& a);
    bool maybe_run_event(time_point currentTime, const scenario::event& e);
};

class override_manipulator : public manipulator
{
public:
    void simulator_added(simulator_index, simulator*, time_point) override;

    void simulator_removed(simulator_index, time_point) override;

    void step_commencing(time_point currentTime) override;

    void override_real_variable(simulator_index, variable_index, double value);
    void override_integer_variable(simulator_index, variable_index, int value);
    void override_boolean_variable(simulator_index, variable_index, bool value);
    void override_string_variable(simulator_index, variable_index, std::string value);

    ~override_manipulator() noexcept override;

private:
    std::unordered_map<simulator_index, simulator*> simulators_;
};

} // namespace cse


#endif //CSECORE_MANIPULATOR_H
