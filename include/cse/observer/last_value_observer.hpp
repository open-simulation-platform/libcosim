/**
 *  \file
 *  \brief Observer-related functionality.
 */
#ifndef CSE_OBSERVER_LAST_VALUE_OBSERVER_HPP
#define CSE_OBSERVER_LAST_VALUE_OBSERVER_HPP

#include <cse/execution.hpp>
#include <cse/model.hpp>
#include <cse/observer/last_value_provider.hpp>

#include <memory>
#include <unordered_map>


namespace cse
{

class slave_value_provider;


/**
 *  An observer implementation, storing the last observed variable values in memory.
 */
class last_value_observer : public last_value_provider
{
public:
    last_value_observer();

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

    void get_real(
        simulator_index sim,
        gsl::span<const value_reference> variables,
        gsl::span<double> values) override;

    void get_integer(
        simulator_index sim,
        gsl::span<const value_reference> variables,
        gsl::span<int> values) override;

    void get_boolean(
        simulator_index sim,
        gsl::span<const value_reference> variables,
        gsl::span<bool> values) override;

    void get_string(
        simulator_index sim,
        gsl::span<const value_reference> variables,
        gsl::span<std::string> values) override;

    ~last_value_observer() noexcept override;

private:
    std::unordered_map<simulator_index, std::unique_ptr<slave_value_provider>> valueProviders_;
};


} // namespace cse
#endif // header guard
