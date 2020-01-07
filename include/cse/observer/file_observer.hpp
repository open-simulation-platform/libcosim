/**
 *  \file
 *  \brief Observers that log simulation results to disk files.
 */
#ifndef CSE_OBSERVER_FILE_OBSERVER_HPP
#define CSE_OBSERVER_FILE_OBSERVER_HPP

#include <cse/observer/observer.hpp>

#include <boost/filesystem/path.hpp>
#include <boost/property_tree/ptree.hpp>

#include <memory>
#include <unordered_map>


namespace cse
{


/**
 * An observer implementation, for saving observed variable values to file in the csv format.
 */
class file_observer : public observer
{
public:
    /**
     * Creates an observer which logs all variable values to file in csv format.
     *
     * \param logDir the directory where log files will be created.
     */
    file_observer(const boost::filesystem::path& logDir);

    /**
     * Creates an observer which logs selected variable values to file in csv format.
     *
     * \param logDir the directory where log files will be created.
     * \param configPath the path to an xml file containing the logging configuration.
     */
    file_observer(const boost::filesystem::path& logDir, const boost::filesystem::path& configPath);

    /// Returns whether the observer is currently recording values.
    bool is_recording();

    /// Starts recording values. Throws an exception if the observer is already recording.
    void start_recording();

    /// Stops recording values. Throws an exception if the observer is not recording.
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

    boost::filesystem::path get_log_path();

    ~file_observer() override;

private:
    struct simulator_logging_config
    {
        std::vector<variable_description> variables;
        size_t decimationFactor;
    };

    simulator_logging_config parse_config(const std::string& simulatorName);

    class slave_value_writer;
    std::unordered_map<simulator_index, std::unique_ptr<slave_value_writer>> valueWriters_;
    std::unordered_map<simulator_index, observable*> simulators_;
    boost::property_tree::ptree ptree_;
    boost::filesystem::path configPath_;
    boost::filesystem::path logDir_;
    bool logFromConfig_ = false;
    size_t defaultDecimationFactor_ = 1;
    bool recording_ = true;
};


} // namespace cse
#endif // header guard
