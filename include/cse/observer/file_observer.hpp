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
 * An observer implementation, for saving observed variable values to file in the preferred format (csv or binary).
 */
class file_observer : public observer
{
public:
    file_observer(const boost::filesystem::path& logDir);

    file_observer(const boost::filesystem::path& logDir, const boost::filesystem::path& configPath);

    void simulator_added(simulator_index, observable*, time_point) override;

    void simulator_removed(simulator_index, time_point) override;

    void variables_connected(variable_id output, variable_id input, time_point) override;

    void variable_disconnected(variable_id input, time_point) override;

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
    void parse_config(const std::string& simulatorName);

    class slave_value_writer;
    std::unordered_map<simulator_index, std::unique_ptr<slave_value_writer>> valueWriters_;
    std::unordered_map<simulator_index, observable*> simulators_;
    std::vector<variable_description> loggableRealVariables_;
    std::vector<variable_description> loggableIntVariables_;
    std::vector<variable_description> loggableBoolVariables_;
    std::vector<variable_description> loggableStringVariables_;
    boost::property_tree::ptree ptree_;
    boost::filesystem::path configPath_;
    boost::filesystem::path logDir_;
    boost::filesystem::path logPath_;
    bool logFromConfig_ = false;
    size_t decimationFactor_;
    size_t defaultDecimationFactor_ = 1;
};


} // namespace cse
#endif // header guard
