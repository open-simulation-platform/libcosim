/**
 *  \file
 *  \brief Observer-related functionality.
 */
#ifndef CSE_OBSERVER_MEMBUFFER_OBSERVER_HPP
#define CSE_OBSERVER_MEMBUFFER_OBSERVER_HPP

#include <memory>
#include <unordered_map>

#include <gsl/span>

#include <cse/execution.hpp>
#include <cse/model.hpp>
#include <cse/observer/time_series_provider.hpp>


namespace cse
{

class slave_value_provider;


/**
 *  An observer implementation, storing all observed variable values in memory.
 */
class membuffer_observer : public time_series_provider
{
public:
    /**
     * Default constructor, sets the internal sample buffer size to 10000
     */
    membuffer_observer();

    /**
     * Constructor for custom sample buffer size
     *
     * \param [in] maximum sample buffer size
     */
    explicit membuffer_observer(size_t);

    void simulator_added(simulator_index, observable*, time_point) override;

    void simulator_removed(simulator_index, time_point) override;

    void variables_connected(variable_id output, variable_id input, time_point) override;

    void variable_disconnected(variable_id input, time_point) override;

    void step_complete(
        step_number lastStep,
        duration lastStepSize,
        time_point currentTime) override;

    /**
     * Retrieves the latest observed values for a range of real variables.
     *
     * \param [in] sim index of the simulator
     * \param [in] variables the variable indices to retrieve values for
     * \param [out] values a collection where the observed values will be stored
     */
    void get_real(
        simulator_index sim,
        gsl::span<const variable_index> variables,
        gsl::span<double> values);

    /**
     * Retrieves the latest observed values for a range of integer variables.
     *
     * \param [in] sim index of the simulator
     * \param [in] variables the variable indices to retrieve values for
     * \param [out] values a collection where the observed values will be stored
     */
    void get_integer(
        simulator_index sim,
        gsl::span<const variable_index> variables,
        gsl::span<int> values);

    std::size_t get_real_samples(
        simulator_index sim,
        variable_index variableIndex,
        step_number fromStep,
        gsl::span<double> values,
        gsl::span<step_number> steps,
        gsl::span<time_point> times) override;

    std::size_t get_integer_samples(
        simulator_index sim,
        variable_index variableIndex,
        step_number fromStep,
        gsl::span<int> values,
        gsl::span<step_number> steps,
        gsl::span<time_point> times) override;

    void get_step_numbers(
        simulator_index sim,
        duration duration,
        gsl::span<step_number> steps) override;

    void get_step_numbers(
        simulator_index sim,
        time_point tBegin,
        time_point tEnd,
        gsl::span<step_number> steps) override;

    ~membuffer_observer() noexcept override;

private:
    size_t bufSize_;
    std::unordered_map<simulator_index, std::unique_ptr<slave_value_provider>> valueProviders_;
};


} // namespace cse
#endif // header guard
