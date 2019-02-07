#ifndef CSECORE_MANIPULATOR_H
#define CSECORE_MANIPULATOR_H

#include <map>
#include <unordered_map>

#include <cse/algorithm.hpp>
#include <cse/model.hpp>
#include <cse/scenario.hpp>

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

} // namespace cse


#endif //CSECORE_MANIPULATOR_H
