#ifndef CSECORE_MANIPULATOR_H
#define CSECORE_MANIPULATOR_H

#include <map>
#include <unordered_map>

#include <cse/execution.hpp>
#include <cse/model.hpp>
#include <cse/scenario.hpp>

namespace cse
{

class manipulable
{
public:
    /**
     *  Exposes a variable for assignment with `set_xxx()`.
     *
     *  The purpose is fundamentally to select which variables get transferred
     *  to remote simulators at each step, so that each individual `set_xxx()`
     *  function call doesn't trigger a new data exchange.
     *
     *  Calling this function more than once for the same variable has no
     *  effect.
     */
    virtual void expose_for_setting(variable_type type, variable_index index) = 0;

    virtual void set_real_input_manipulator(
            variable_index index,
            std::function<double(double)> manipulator) = 0;

    virtual void set_integer_input_manipulator(
            variable_index index,
            std::function<int(int)> manipulator) = 0;

    virtual void set_boolean_input_manipulator(
            variable_index index,
            std::function<bool(bool)> manipulator) = 0;

    virtual void set_string_input_manipulator(
            variable_index index,
            std::function<std::string(std::string_view)> manipulator) = 0;

    virtual void set_real_output_manipulator(
            variable_index index,
            std::function<double(double)> manipulator) = 0;

    virtual void set_integer_output_manipulator(
            variable_index index,
            std::function<int(int)> manipulator) = 0;

    virtual void set_boolean_output_manipulator(
            variable_index index,
            std::function<bool(bool)> manipulator) = 0;

    virtual void set_string_output_manipulator(
            variable_index index,
            std::function<std::string(std::string_view)> manipulator) = 0;

    virtual ~manipulable() noexcept {}
};

class manipulator
{
public:
    /// A simulator was added to the execution.
    virtual void simulator_added(simulator_index, manipulable*, time_point) = 0;

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
    void simulator_added(simulator_index, manipulable*, time_point) override;

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
    std::unordered_map<simulator_index, manipulable*> simulators_;

    void execute_action(manipulable* sim, const scenario::variable_action& a);
    bool maybe_run_event(time_point currentTime, const scenario::event& e);
};

} // namespace cse


#endif //CSECORE_MANIPULATOR_H
