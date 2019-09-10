/**
 *  \file
 *  \brief Defines the `manipulator` interface.
 */

#ifndef CSECORE_MANIPULATOR_H
#define CSECORE_MANIPULATOR_H

#include <cse/algorithm.hpp>
#include <cse/model.hpp>
#include <cse/scenario.hpp>

#include <boost/filesystem/path.hpp>

#include <memory>
#include <string>
#include <unordered_map>

namespace cse
{

/**
 *  An interface for manipulators.
 *
 *  The methods in this interface represent various events that the manipulator
 *  may react to in some way. It may modify the slaves' variable values
 *  through the `simulator` interface at any time.
 */
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

/**
 *  A manipulator implementation handling execution and control of scenarios.
 */
class scenario_manager : public manipulator
{
public:
    void simulator_added(simulator_index, simulator*, time_point) override;

    void simulator_removed(simulator_index, time_point) override;

    void step_commencing(
        time_point currentTime) override;

    /// Constructor.
    scenario_manager();

    scenario_manager(const scenario_manager&) = delete;
    scenario_manager& operator=(const scenario_manager&) = delete;

    scenario_manager(scenario_manager&&) noexcept;
    scenario_manager& operator=(scenario_manager&&) noexcept;

    ~scenario_manager() noexcept override;

    /**
     * Load a scenario for execution.
     *
     * \param s
     *  The in-memory constructed scenario.
     * \param currentTime
     *  The time point at which the scenario will start. The scenario's events
     *  will be executed relative to this time point.
     */
    void load_scenario(const scenario::scenario& s, time_point currentTime);

    /**
     * Load a scenario for execution.
     *
     * \param scenarioFile
     *  The path to a proprietary `json` file defining the scenario.
     * \param currentTime
     *  The time point at which the scenario will start. The scenario's events
     *  will be executed relative to this time point.
     */
    void load_scenario(const boost::filesystem::path& scenarioFile, time_point currentTime);

    /// Return if a scenario is running.
    bool is_scenario_running();

    /// Abort the execution of a running scenario.
    void abort_scenario();

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

/**
 *  A manipulator implementation handling overrides of variable values.
 */
class override_manipulator : public manipulator
{
public:
    void simulator_added(simulator_index, simulator*, time_point) override;

    void simulator_removed(simulator_index, time_point) override;

    void step_commencing(time_point currentTime) override;

    /// Override the value of a variable with type `real`
    void override_real_variable(simulator_index, value_reference, double value);
    /// Override the value of a variable with type `integer`
    void override_integer_variable(simulator_index, value_reference, int value);
    /// Override the value of a variable with type `boolean`
    void override_boolean_variable(simulator_index, value_reference, bool value);
    /// Override the value of a variable with type `string`
    void override_string_variable(simulator_index, value_reference, const std::string& value);
    /// Reset override of a variable
    void reset_variable(simulator_index, variable_type, value_reference);

    ~override_manipulator() noexcept override;

private:
    void add_action(
        simulator_index index,
        value_reference variable,
        variable_type type,
        const std::variant<
            scenario::real_modifier,
            scenario::integer_modifier,
            scenario::boolean_modifier,
            scenario::string_modifier>& m);

    std::unordered_map<simulator_index, simulator*> simulators_;
    std::vector<scenario::variable_action> actions_;
    std::mutex lock_;
};

} // namespace cse


#endif //CSECORE_MANIPULATOR_H
