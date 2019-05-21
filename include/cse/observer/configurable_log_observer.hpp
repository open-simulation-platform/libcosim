//
// Created by STENBRO on 5/15/2019.
//

#ifndef CSECORE_CONFIGURABLE_LOG_OBSERVER_HPP
#define CSECORE_CONFIGURABLE_LOG_OBSERVER_HPP

#include <cse/algorithm.hpp>
#include <cse/observer/observer.hpp>

#include <boost/filesystem/path.hpp>

#include <unordered_map>


namespace cse
{

class configurable_log_observer : public observer
{
public:
    configurable_log_observer(boost::filesystem::path& logPath, int rate, int limit);

    void simulator_added(simulator_index, observable*, time_point) override;

    void simulator_removed(simulator_index, time_point) override;

    void variables_connected(variable_id output, variable_id input, time_point) override;

    void variable_disconnected(variable_id input, time_point) override;

    void add_variable(variable_description, time_point);

    void remove_variable(variable_description, time_point);

    void step_complete(
        step_number lastStep,
        duration lastStepSize,
        time_point currentTime) override;

    void simulator_step_complete(
        simulator_index index,
        step_number lastStep,
        duration lastStepSize,
        time_point currentTime) override;

    ~configurable_log_observer();

private:
    std::vector<variable_description> realVars_;
    std::vector<variable_description> intVars_;
    std::vector<variable_description> boolVars_;
    std::vector<variable_description> strVars_;
    boost::filesystem::path logPath_;
    int rate_;
    int limit_;
};

} // namespace cse

#endif //CSECORE_CONFIGURABLE_LOG_OBSERVER_HPP
