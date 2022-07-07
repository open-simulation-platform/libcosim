/**
 *  \file
 *  Observers that log simulation results to disk files.
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_OBSERVER_FILE_OBSERVER_HPP
#define COSIM_OBSERVER_FILE_OBSERVER_HPP

#include <cosim/fs_portability.hpp>
#include <cosim/observer/observer.hpp>

#include <atomic>
#include <memory>
#include <unordered_map>


namespace cosim
{

class file_observer;

class file_observer_config
{
public:
    file_observer_config() = default;

    file_observer_config& set_timestamped_filenames(bool flag)
    {
        timeStampedFileNames_ = flag;
        return *this;
    }

    file_observer_config& log_all_simulator_variables(const std::string& simulatorName, std::optional<size_t> decimationFactor = std::nullopt)
    {
        variablesToLog_[simulatorName].first = decimationFactor.value_or(defaultDecimationFactor_);
        return *this;
    }

    file_observer_config& log_simulator_variable(const std::string& simulatorName, const std::string& variableName, std::optional<size_t> decimationFactor = std::nullopt)
    {
        variablesToLog_[simulatorName].first = decimationFactor.value_or(defaultDecimationFactor_);
        variablesToLog_[simulatorName].second.emplace_back(variableName);
        return *this;
    }

    /**
     *
     * @param configPath the path to an xml file containing the logging configuration.
     * @return a file_observer_config
     */
    static file_observer_config parse(const filesystem::path& configPath);

private:
    size_t defaultDecimationFactor_{1};
    bool timeStampedFileNames_{true};
    std::unordered_map<std::string, std::pair<size_t, std::vector<std::string>>> variablesToLog_;

    [[nodiscard]] bool should_log_simulator(const std::string& name) const
    {
        return variablesToLog_.count(name);
    }

    friend class file_observer;
};


/**
 * An observer implementation, for saving observed variable values to file in csv format.
 *
 * Recording may be toggled on or off mid simulation. This functionality is thread safe.
 */
class file_observer : public observer
{
public:
    /**
     * Creates an observer which logs all variable values to file in csv format.
     *
     * \param logDir the directory where log files will be created.
     * \param config an optional logging configuration.
     */
    explicit file_observer(const cosim::filesystem::path& logDir, const std::optional<file_observer_config>& config = std::nullopt);

    /**
     * Returns whether the observer is currently recording values.
     *
     * This method can safely be called from different threads.
     */
    bool is_recording();

    /**
     * Starts recording values. Throws an exception if the observer is already recording.
     *
     * This method can safely be called from different threads.
     */
    void start_recording();

    /**
     * Stops recording values. Throws an exception if the observer is not recording.
     *
     * This method can safely be called from different threads.
     */
    void stop_recording();

    void simulator_added(simulator_index, observable*, time_point) override;

    void simulator_removed(simulator_index, time_point) override;

    void variables_connected(variable_id output, variable_id input, time_point) override;

    void variable_disconnected(variable_id input, time_point) override;

    void simulation_initialized(
        step_number firstStep,
        time_point startTime) override;

    void step_complete(
        step_number lastStep,
        duration lastStepSize,
        time_point currentTime) override;

    void simulator_step_complete(
        simulator_index index,
        step_number lastStep,
        duration lastStepSize,
        time_point currentTime) override;

    cosim::filesystem::path get_log_path();

    ~file_observer() override;

private:
    struct simulator_logging_config
    {
        std::vector<variable_description> variables;
        size_t decimationFactor;
        bool timeStampedFileNames;
    };

    simulator_logging_config parse_config(const std::string& simulatorName);

    class slave_value_writer;
    std::unordered_map<simulator_index, std::unique_ptr<slave_value_writer>> valueWriters_;
    std::unordered_map<simulator_index, observable*> simulators_;
    std::optional<file_observer_config> config_;
    cosim::filesystem::path logDir_;
    std::atomic<bool> recording_ = true;
};


} // namespace cosim

#endif // COSIM_OBSERVER_FILE_OBSERVER_HPP
