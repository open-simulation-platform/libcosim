#ifndef LIBCOSIM_MANIPULATOR_SCENARIO_MANAGER_HPP
#define LIBCOSIM_MANIPULATOR_SCENARIO_MANAGER_HPP

#include <cse/manipulator/manipulator.hpp>
#include <cse/scenario.hpp>

#include <boost/filesystem/path.hpp>


namespace cosim
{

/**
 *  A manipulator implementation handling execution and control of scenarios.
 */
class scenario_manager : public manipulator
{
public:
    void simulator_added(simulator_index, manipulable*, time_point) override;

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
     * This function may only be called after the object has been added to an
     * execution with `execution::add_manipulator()`.
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
     * This function may only be called after the object has been added to an
     * execution with `execution::add_manipulator()`.
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

} // namespace cse

#endif
