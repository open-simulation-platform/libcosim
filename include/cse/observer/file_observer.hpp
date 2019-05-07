/**
 *  \file
 *  \brief Observers that log simulation results to disk files.
 */
#ifndef CSE_OBSERVER_FILE_OBSERVER_HPP
#define CSE_OBSERVER_FILE_OBSERVER_HPP

#include <memory>
#include <unordered_map>

#include <cse/observer/observer.hpp>
#include <cse/algorithm.hpp>

#include <boost/filesystem/path.hpp>
#include <boost/property_tree/ptree.hpp>


namespace cse
{


/**
 * An observer implementation, for saving observed variable values to file in the preferred format (csv or binary).
 */
class file_observer : public observer
{
public:
    file_observer(boost::filesystem::path& logDir, size_t limit);

    file_observer(boost::filesystem::path& configPath, boost::filesystem::path& logDir);

    void simulator_added(simulator_index, observable*, time_point);

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
    boost::filesystem::path get_config_path();

    ~file_observer();

private:
    void parse_config();

    class slave_value_writer;
    std::unordered_map<simulator_index, std::unique_ptr<slave_value_writer>> valueWriters_;
    std::unordered_map<simulator_index, simulator*> simulators_;
    boost::property_tree::ptree ptree_;
    boost::filesystem::path logDir_;
    boost::filesystem::path logPath_;
    boost::filesystem::path configPath_;
    bool binary_;
    bool logFromConfig_ = false;
    size_t limit_;
};


} // namespace cse
#endif // header guard
