/**
 *  \file
 *  \brief Observer-related functionality.
 */
#ifndef CSE_OBSERVER_HPP
#define CSE_OBSERVER_HPP

#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <cse/execution.hpp>
#include <cse/model.hpp>

#include <boost/filesystem/path.hpp>


namespace cse
{

/// Interface for observable entities in a simulation.
class observable
{
public:
    /// Returns the entity's name.
    virtual std::string name() const = 0;

    /// Returns a description of the entity.
    virtual cse::model_description model_description() const = 0;

    /**
     *  Exposes a variable for retrieval with `get_xxx()`.
     *
     *  The purpose is fundamentally to select which variables get transferred
     *  from remote simulators at each step, so that each individual `get_xxx()`
     *  function call doesn't trigger a separate RPC operation.
     */
    virtual void expose_for_getting(variable_type, variable_index) = 0;

    /**
     *  Returns the value of a real variable.
     *
     *  The variable must previously have been exposed with `expose_for_getting()`.
     */
    virtual double get_real(variable_index) const = 0;

    /**
     *  Returns the value of an integer variable.
     *
     *  The variable must previously have been exposed with `expose_for_getting()`.
     */
    virtual int get_integer(variable_index) const = 0;

    /**
     *  Returns the value of a boolean variable.
     *
     *  The variable must previously have been exposed with `expose_for_getting()`.
     */
    virtual bool get_boolean(variable_index) const = 0;

    /**
     *  Returns the value of a string variable.
     *
     *  The variable must previously have been exposed with `expose_for_getting()`.
     *
     *  The returned `std::string_view` is only guaranteed to remain valid
     *  until the next call of this or any other of the object's methods.
     */
    virtual std::string_view get_string(variable_index) const = 0;

    virtual ~observable() noexcept {}
};


/**
 *  An interface for observers.
 *
 *  The methods in this interface represent various events that the observer
 *  may record or react to in some way. It may query the slaves for variable
 *  values and other info through the `observable` interface at any time.
 */
class observer
{
public:
    /// A simulator was added to the execution.
    virtual void simulator_added(simulator_index, observable*, time_point) = 0;

    /// A simulator was removed from the execution.
    virtual void simulator_removed(simulator_index, time_point) = 0;

    /// A variable connection was established.
    virtual void variables_connected(variable_id output, variable_id input, time_point) = 0;

    /// A variable connection was broken.
    virtual void variable_disconnected(variable_id input, time_point) = 0;

    /// A time step is complete, and a communication point was reached.
    virtual void step_complete(
        step_number lastStep,
        duration lastStepSize,
        time_point currentTime) = 0;

    virtual ~observer() noexcept {}
};

class slave_value_provider;

/**
 * An observer implementation, for saving observed variable values to file in the preferred format (csv or binary).
 */
class file_observer : public observer
{
public:
    file_observer(boost::filesystem::path logDir, bool binary, size_t limit);

    void simulator_added(simulator_index, observable*, time_point) override;

    void simulator_removed(simulator_index, time_point) override;

    void variables_connected(variable_id output, variable_id input, time_point) override;

    void variable_disconnected(variable_id input, time_point) override;

    void step_complete(
        step_number lastStep,
        duration lastStepSize,
        time_point currentTime) override;

    boost::filesystem::path get_log_path();

    ~file_observer();

private:
    class slave_value_writer;
    std::unordered_map<simulator_index, std::unique_ptr<slave_value_writer>> valueWriters_;
    boost::filesystem::path logDir_;
    boost::filesystem::path logPath_;
    bool binary_;
    size_t limit_;
};

/**
 *  An interface for time series providers.
 *
 *  The methods in this interface represent ways to extract data from an
 *  observer providing time series data.
 */

class time_series_provider : public observer
{
public:
    /**
     * Retrieves a series of observed values, step numbers and times for a real variable.
     *
     * \param [in] sim index of the simulator
     * \param [in] variableIndex the variable index
     * \param [in] fromStep the step number to start from
     * \param [out] values the series of observed values
     * \param [out] steps the corresponding step numbers
     * \param [out] times the corresponding simulation times
     *
     * Returns the number of samples actually read, which may be smaller
     * than the sizes of `values` and `steps`.
     */
    virtual std::size_t get_real_samples(
        simulator_index sim,
        variable_index variableIndex,
        step_number fromStep,
        gsl::span<double> values,
        gsl::span<step_number> steps,
        gsl::span<time_point> times) = 0;

    /**
     * Retrieves a series of observed values, step numbers and times for an integer variable.
     *
     * \param [in] sim index of the simulator
     * \param [in] variableIndex the variable index
     * \param [in] fromStep the step number to start from
     * \param [out] values the series of observed values
     * \param [out] steps the corresponding step numbers
     * \param [out] times the corresponding simulation times
     *
     * Returns the number of samples actually read, which may be smaller
     * than the sizes of `values` and `steps`.
     */
    virtual std::size_t get_integer_samples(
        simulator_index sim,
        variable_index variableIndex,
        step_number fromStep,
        gsl::span<int> values,
        gsl::span<step_number> steps,
        gsl::span<time_point> times) = 0;

    /**
     * Retrieves the step numbers for a range given by a duration.
     *
     * Helper function which can be used in conjunction with `get_xxx_samples()`
     * when it is desired to retrieve the latest available samples given a certain duration.
     *
     * \param [in] sim index of the simulator
     * \param [in] duration the duration to get step numbers for
     * \param [out] steps the corresponding step numbers
     */
    virtual void get_step_numbers(
        simulator_index sim,
        duration duration,
        gsl::span<step_number> steps) = 0;

    /**
     * Retrieves the step numbers for a range given by two points in time.
     *
     * Helper function which can be used in conjunction with `get_xxx_samples()`
     * when it is desired to retrieve samples between two points in time.
     *
     * \param [in] sim index of the simulator
     * \param [in] tBegin the start of the range
     * \param [in] tEnd the end of the range
     * \param [out] steps the corresponding step numbers
     */
    virtual void get_step_numbers(
        simulator_index sim,
        time_point tBegin,
        time_point tEnd,
        gsl::span<step_number> steps) = 0;
};

/**
 *  An observer implementation, storing all observed variable values in memory.
 */
class membuffer_observer : public time_series_provider
{
public:
    membuffer_observer();

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

    ~membuffer_observer() noexcept;

private:
    std::unordered_map<simulator_index, std::unique_ptr<slave_value_provider>> valueProviders_;
};

/**
 *  An observer implementation, storing all observed variable values for a user-specified set of variables in memory.
 */
class time_series_observer : public time_series_provider
{
public:
    time_series_observer();

    void simulator_added(simulator_index, observable*, time_point) override;

    void simulator_removed(simulator_index, time_point) override;

    void variables_connected(variable_id output, variable_id input, time_point) override;

    void variable_disconnected(variable_id input, time_point) override;

    void step_complete(
        step_number lastStep,
        duration lastStepSize,
        time_point currentTime) override;

    void start_observing(variable_id id);

    void stop_observing(variable_id id);

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

    ~time_series_observer() noexcept;

private:
    class single_slave_observer;
    std::unordered_map<simulator_index, std::unique_ptr<single_slave_observer>> slaveObservers_;
};

} // namespace cse
#endif // header guard
