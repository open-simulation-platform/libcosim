/**
 *  \file
 *  C library header.
 *  \ingroup csecorec
 */
#ifndef CSE_H
#define CSE_H

#include "cse_types.h"

/// \defgroup csecorec C library

#ifdef __cplusplus
extern "C" {
#endif

/**
 *  Returns the error code associated with the last reported error.
 *
 *  Most functions in this library will indicate that an error occurred by
 *  returning -1 or `NULL`, after which this function can be called to
 *  obtain more detailed information about the problem.
 *
 *  This function must be called from the thread in which the error occurred,
 *  and before any new calls to functions in this library (with the exception
 *  of `cse_last_error_message()`).
 *
 *  \returns
 *      The error code associated with the last reported error.
 */
cse_errc cse_last_error_code();


/**
 *  Returns a textual description of the last reported error.
 *
 *  Most functions in this library will indicate that an error occurred by
 *  returning -1 or `NULL`, after which this function can be called to
 *  obtain more detailed information about the problem.
 *
 *  This function must be called from the thread in which the error occurred,
 *  and before any new calls to functions in this library (with the exception
 *  of `cse_last_error_code()`).
 *
 *  \returns
 *      A textual description of the last reported error.  The pointer is
 *      only guaranteed to remain valid until the next time a function in
 *      this library is called (with the exception of `cse_last_error_code()`).
 */
const char* cse_last_error_message();

/**
 *  Creates a new execution.
 * 
 *  \param [in] startTime
 *      The (logical) time point at which the simulation should start.
 *  \param [in] stepSize
 *      The execution step size.
 *  \returns
 *      A pointer to an object which holds the execution state,
 *      or NULL on error.
 */
cse_execution* cse_execution_create(
    cse_time_point startTime,
    cse_duration stepSize);

/**
 *  Creates a new execution based on a SystemStructure.ssd file.
 *
 *  \param [in] sspDir
 *      Path to the directory holding SystemStructure.ssd
 *  \param [in] startTime
 *      The (logical) time point at which the simulation should start.
 *  \returns
 *      A pointer to an object which holds the execution state,
 *      or NULL on error.
 */
cse_execution* cse_ssp_execution_create(
    const char* sspDir,
    cse_time_point startTime);

/**
 *  Destroys an execution.
 *
 *  \returns
 *      0 on success and -1 on error.
 */
int cse_execution_destroy(cse_execution* execution);


/**
 *  Creates a new local slave.
 *
 *  \param [in] fmuPath
 *      Path to FMU.
 *
 *  \returns
 *      A pointer to an object which holds the local slave object,
 *      or NULL on error.
 */
cse_slave* cse_local_slave_create(const char* fmuPath);

/**
 *  Destroys a local slave.
 *
 *  \returns
 *       0 on success and -1 on error.
 */
int cse_local_slave_destroy(cse_slave* slave);


/**
 *  Loads a co-simulation FMU, instantiates a slave based on it, and adds it
 *  to an execution.
 *
 *  The slave is owned by the execution and is destroyed along with it.
 *
 *  \param [in] execution
 *      The execution to which the slave should be added.
 *  \param [in] slave
 *      A pointer to a slave, which may not be null. The slave may not previously have been added to any execution.
 *
 *  \returns
 *      The slave's unique index in the execution, or -1 on error.
 */
cse_slave_index cse_execution_add_slave(
    cse_execution* execution,
    cse_slave* slave);


/**
 *  Advances an execution a number of time steps.
 *
 *  \param [in] execution
 *      The execution to be stepped.
 *  \param [in] numSteps
 *      The number of steps to advance the simulation execution.
 *
 *  \returns
 *      0 on success and -1 on error.
 */
int cse_execution_step(cse_execution* execution, size_t numSteps);


/**
 *  Starts an execution.
 *
 *  The execution will run until `cse_execution_stop()` is called.
 *
 *  \param [in] execution
 *      The execution to be started.
 *
 *  \returns
 *      0 on success and -1 on error.
 */
int cse_execution_start(cse_execution* execution);


/**
 *  Stops an execution.
 *
 *  \param [in] execution
 *      The execution to be stopped.
 *
 *  \returns
 *      0 on success and -1 on error.
 */
int cse_execution_stop(cse_execution* execution);


/// Enables real time simulation for an execution.
int cse_execution_enable_real_time_simulation(cse_execution* execution);


/// Disables real time simulation for an execution.
int cse_execution_disable_real_time_simulation(cse_execution* execution);

/// Sets a custom real time factor.
int cse_execution_set_real_time_factor_target(cse_execution *execution, double realTimeFactor);


/**
 *  Returns execution status.
 *
 *  \param [in] execution
 *      The execution to get status from.
 *  \param [out] status
 *      A pointer to a cse_execution_status that will be filled with actual
 *      execution status.
 *
 *  \returns
 *      0 on success and -1 on error.
 */
int cse_execution_get_status(
    cse_execution* execution,
    cse_execution_status* status);


/// Returns the number of variables for a slave which has been added to an execution, or -1 on error.
int cse_slave_get_num_variables(cse_execution* execution, cse_slave_index slave);

/**
 *  Returns variable metadata for a slave.
 *
 *  \param [in] execution
 *      The execution which the slave has been added to.
 *  \param [in] slave
 *      The index of the slave.
 *  \param [out] variables
 *      A pointer to an array of length `numVariables` which will be filled with actual `cse_variable_description` values.
 *  \param [in] numVariables
 *      The length of the `variables` array.
 *
 *  \returns
 *      The number of variables written to `variables` array or -1 on error.
 */
int cse_slave_get_variables(cse_execution* execution, cse_slave_index slave, cse_variable_description variables[], size_t numVariables);


/// Returns the number of slaves which have been added to an execution.
size_t cse_execution_get_num_slaves(cse_execution* execution);

/**
 *  Returns slave infos.
 *
 *  \param [in] execution
 *      The execution to get slave infos from.
 *  \param [out] infos
 *      A pointer to an array of length `num_slaves` which will be filled with actual `slave_info` values.
 *  \param [in] numSlaves
 *      The length of the `infos` array.
 *
 *  \returns
 *      0 on success and -1 on error.
 */
int cse_execution_get_slave_infos(cse_execution* execution, cse_slave_info infos[], size_t numSlaves);


/**
 *  Sets the values of real variables for one slave.
 *
 *  \param [in] manipulator
 *      The manipulator.
 *  \param [in] slaveIndex
 *      The slave.
 *  \param [in] variables
 *      A pointer to an array of length `nv` that contains the (slave-specific)
 *      indices of variables to set.
 *  \param [in] nv
 *      The length of the `variables` and `values` arrays.
 *  \param [out] values
 *      A pointer to an array of length `nv` with the
 *      values of the variables specified in `variables`, in the same order.
 *
 *  \returns
 *      0 on success and -1 on error.
 */
int cse_manipulator_slave_set_real(
    cse_manipulator* manipulator,
    cse_slave_index slaveIndex,
    const cse_variable_index variables[],
    size_t nv,
    const double values[]);

/**
 *  Sets the values of integer variables for one slave.
 *
 *  \param [in] manipulator
 *      The manipulator.
 *  \param [in] slaveIndex
 *      The slave.
 *  \param [in] variables
 *      A pointer to an array of length `nv` that contains the (slave-specific)
 *      indices of variables to set.
 *  \param [in] nv
 *      The length of the `variables` and `values` arrays.
 *  \param [out] values
 *      A pointer to an array of length `nv` with the
 *      values of the variables specified in `variables`, in the same order.
 *
 *  \returns
 *      0 on success and -1 on error.
 */
int cse_manipulator_slave_set_integer(
    cse_manipulator* manipulator,
    cse_slave_index slaveIndex,
    const cse_variable_index variables[],
    size_t nv,
    const int values[]);

/**
 *  Sets the values of boolean variables for one slave.
 *
 *  \param [in] manipulator
 *      The manipulator.
 *  \param [in] slaveIndex
 *      The slave.
 *  \param [in] variables
 *      A pointer to an array of length `nv` that contains the (slave-specific)
 *      indices of variables to set.
 *  \param [in] nv
 *      The length of the `variables` and `values` arrays.
 *  \param [out] values
 *      A pointer to an array of length `nv` with the
 *      values of the variables specified in `variables`, in the same order.
 *
 *  \returns
 *      0 on success and -1 on error.
 */
int cse_manipulator_slave_set_boolean(
    cse_manipulator* manipulator,
    cse_slave_index slaveIndex,
    const cse_variable_index variables[],
    size_t nv,
    const bool values[]);

/**
 *  Sets the values of string variables for one slave.
 *
 *  \param [in] manipulator
 *      The manipulator.
 *  \param [in] slaveIndex
 *      The slave.
 *  \param [in] variables
 *      A pointer to an array of length `nv` that contains the (slave-specific)
 *      indices of variables to set.
 *  \param [in] nv
 *      The length of the `variables` and `values` arrays.
 *  \param [out] values
 *      A pointer to an array of length `nv` with the
 *      values of the variables specified in `variables`, in the same order.
 *
 *  \returns
 *      0 on success and -1 on error.
 */
int cse_manipulator_slave_set_string(
    cse_manipulator* manipulator,
    cse_slave_index slaveIndex,
    const cse_variable_index variables[],
    size_t nv,
    const char* values[]);

/**
 *  Resets any previously overridden variable values of a certain type for one slave.
 *
 *  \param [in] manipulator
 *      The manipulator.
 *  \param [in] slaveIndex
 *      The slave.
 *  \param [in] type
 *      The variable type.
 *  \param [in] variables
 *      A pointer to an array of length `nv` that contains the (slave-specific)
 *      indices of variables to reset.
 *  \param [in] nv
 *      The length of the `variables` array.
 *
 *  \returns
 *      0 on success and -1 on error.
 */
int cse_manipulator_slave_reset(
    cse_manipulator* manipulator,
    cse_slave_index slaveIndex,
    cse_variable_type type,
    const cse_variable_index variables[],
    size_t nv);

/**
 *  Retrieves the values of real variables for one slave.
 *
 *  \param [in] observer
 *      The observer.
 *  \param [in] slave
 *      The slave.
 *  \param [in] variables
 *      A pointer to an array of length `nv` that contains the (slave-specific)
 *      indices of variables to retrieve.
 *  \param [in] nv
 *      The length of the `variables` and `values` arrays.
 *  \param [out] values
 *      A pointer to an array of length `nv` which will be filled with the
 *      values of the variables specified in `variables`, in the same order.
 *
 *  \returns
 *      0 on success and -1 on error.
 */
int cse_observer_slave_get_real(
    cse_observer* observer,
    cse_slave_index slave,
    const cse_variable_index variables[],
    size_t nv,
    double values[]);

/**
 *  Retrieves the values of integer variables for one slave.
 *
 *  \param [in] observer
 *      The observer.
 *  \param [in] slave
 *      The slave index.
 *  \param [in] variables
 *      A pointer to an array of length `nv` that contains the (slave-specific)
 *      indices of variables to retrieve.
 *  \param [in] nv
 *      The length of the `variables` and `values` arrays.
 *  \param [out] values
 *      A pointer to an array of length `nv` which will be filled with the
 *      values of the variables specified in `variables`, in the same order.
 *
 *  \returns
 *      0 on success and -1 on error.
 */
int cse_observer_slave_get_integer(
    cse_observer* observer,
    cse_slave_index slave,
    const cse_variable_index variables[],
    size_t nv,
    int values[]);

/**
 *  Retrieves the values of boolean variables for one slave.
 *
 *  \param [in] observer
 *      The observer.
 *  \param [in] slave
 *      The slave index.
 *  \param [in] variables
 *      A pointer to an array of length `nv` that contains the (slave-specific)
 *      indices of variables to retrieve.
 *  \param [in] nv
 *      The length of the `variables` and `values` arrays.
 *  \param [out] values
 *      A pointer to an array of length `nv` which will be filled with the
 *      values of the variables specified in `variables`, in the same order.
 *
 *  \returns
 *      0 on success and -1 on error.
 */
int cse_observer_slave_get_boolean(
    cse_observer* observer,
    cse_slave_index slave,
    const cse_variable_index variables[],
    size_t nv,
    bool values[]);

/**
 *  Retrieves the values of string variables for one slave.
 *
 *  \param [in] observer
 *      The observer.
 *  \param [in] slave
 *      The slave index.
 *  \param [in] variables
 *      A pointer to an array of length `nv` that contains the (slave-specific)
 *      indices of variables to retrieve.
 *  \param [in] nv
 *      The length of the `variables` and `values` arrays.
 *  \param [out] values
 *      A pointer to an array of length `nv` which will be filled with pointers
 *      to the values of the variables specified in `variables`, in the same order.
 *      The pointers are valid until the next call to `cse_observer_slave_get_string()`.
 *
 *  \returns
 *      0 on success and -1 on error.
 */
int cse_observer_slave_get_string(
    cse_observer* observer,
    cse_slave_index slave,
    const cse_variable_index variables[],
    size_t nv,
    const char* values[]);

/**
 * Retrieves a series of observed values, step numbers and times for a real variable.
 *
 * \param [in] observer the observer
 * \param [in] slave index of the slave
 * \param [in] variableIndex the variable index
 * \param [in] fromStep the step number to start from
 * \param [in] nSamples the number of samples to read
 * \param [out] values the series of observed values
 * \param [out] steps the corresponding step numbers
 * \param [out] times the corresponding simulation times
 *
 * \returns
 *      The number of samples actually read, which may be smaller than `nSamples`.
 */
int64_t cse_observer_slave_get_real_samples(
    cse_observer* observer,
    cse_slave_index slave,
    cse_variable_index variableIndex,
    cse_step_number fromStep,
    size_t nSamples,
    double values[],
    cse_step_number steps[],
    cse_time_point times[]);

/**
 * Retrieves a series of observed values, step numbers and times for an integer variable.
 *
 * \param [in] observer the observer
 * \param [in] slave index of the slave
 * \param [in] variableIndex the variable index
 * \param [in] fromStep the step number to start from
 * \param [in] nSamples the number of samples to read
 * \param [out] values the series of observed values
 * \param [out] steps the corresponding step numbers
 * \param [out] times the corresponding simulation times
 *
 * \returns
 *      The number of samples actually read, which may be smaller than `nSamples`.
 */
int64_t cse_observer_slave_get_integer_samples(
    cse_observer* observer,
    cse_slave_index slave,
    cse_variable_index variableIndex,
    cse_step_number fromStep,
    size_t nSamples,
    int values[],
    cse_step_number steps[],
    cse_time_point times[]);

/**
 * Retrieves two time-synchronized series of observed values for two real variables.
 *
 * \param [in] observer the observer
 * \param [in] slave1 index of the first slave
 * \param [in] variableIndex1 the first variable index
 * \param [in] slave2 index of the second slave
 * \param [in] variableIndex2 the second variable index
 * \param [in] fromStep the step number to start from
 * \param [in] nSamples the number of samples to read
 * \param [out] values1 the first series of observed values
 * \param [out] values2 the second series of observed values
 *
 * \returns
 *      The number of samples actually read, which may be smaller than `nSamples`.
 */
int64_t cse_observer_slave_get_real_synchronized_series(
    cse_observer* observer,
    cse_slave_index slave1,
    cse_variable_index variableIndex1,
    cse_slave_index slave2,
    cse_variable_index variableIndex2,
    cse_step_number fromStep,
    size_t nSamples,
    double values1[],
    double values2[]);

/**
 * Retrieves the step numbers for a range given by a duration.
 *
 * Helper function which can be used in conjunction with `cse_observer_slave_get_xxx_samples()`
 * when it is desired to retrieve the latest available samples given a certain duration.
 *
 * \note
 * It is assumed that `steps` has a length of 2.
 *
 * \param [in] observer the observer
 * \param [in] slave index of the slave
 * \param [in] duration the duration to get step numbers for
 * \param [out] steps the corresponding step numbers
 */
int cse_observer_get_step_numbers_for_duration(
    cse_observer* observer,
    cse_slave_index slave,
    cse_duration duration,
    cse_step_number steps[]);

/**
 * Retrieves the step numbers for a range given by two points in time.
 *
 * Helper function which can be used in conjunction with `cse_observer_slave_get_xxx_samples()`
 * when it is desired to retrieve samples between two points in time.
 *
 * \note
 * It is assumed that `steps` has a length of 2.
 *
 * \param [in] observer the observer
 * \param [in] slave index of the simulator
 * \param [in] begin the start of the range
 * \param [in] end the end of the range
 * \param [out] steps the corresponding step numbers
 */
int cse_observer_get_step_numbers(
    cse_observer* observer,
    cse_slave_index slave,
    cse_time_point begin,
    cse_time_point end,
    cse_step_number steps[]);

/**
 *  Connects one real output variable to one real input variable.
 *
 *  \param [in] execution
 *      The execution.
 *  \param [in] outputSlaveIndex
 *      The source slave.
 *  \param [in] outputVariableIndex
 *      The source variable.
 *  \param [in] inputSlaveIndex
 *      The destination slave.
 *  \param [in] inputVariableIndex
 *      The destination variable.
 *
 *  \returns
 *      0 on success and -1 on error.
 */
int cse_execution_connect_real_variables(
    cse_execution* execution,
    cse_slave_index outputSlaveIndex,
    cse_variable_index outputVariableIndex,
    cse_slave_index inputSlaveIndex,
    cse_variable_index inputVariableIndex);

/**
 *  Connects one integer output variable to one integer input variable.
 *
 *  \param [in] execution
 *      The execution.
 *  \param [in] outputSlaveIndex
 *      The source slave.
 *  \param [in] outputVariableIndex
 *      The source variable.
 *  \param [in] inputSlaveIndex
 *      The destination slave.
 *  \param [in] inputVariableIndex
 *      The destination variable.
 *
 *  \returns
 *      0 on success and -1 on error.
 */
int cse_execution_connect_integer_variables(
    cse_execution* execution,
    cse_slave_index outputSlaveIndex,
    cse_variable_index outputVariableIndex,
    cse_slave_index inputSlaveIndex,
    cse_variable_index inputVariableIndex);


/// Creates an observer which stores the last observed value for all variables.
cse_observer* cse_last_value_observer_create();

/**
 * Creates an observer which logs variable values to file in csv format.
 *
 * @param logDir
 *      The directory where log files will be created.
 * \returns
 *      The created observer.
 */
cse_observer* cse_file_observer_create(const char* logDir);

/**
 * Creates an observer which logs variable values to file in csv format. Variables to be logged
 * are specified in the supplied log config xml file.
 *
 * @param logDir
 *      The directory where log files will be created.
 * @param cfgDir
 *      The path to the provided config xml file.
 * \returns
 *      The created observer.
 */
cse_observer* cse_file_observer_create_from_cfg(const char* logDir, const char* cfgPath);

/**
 * Creates an observer which buffers variable values in memory.
 *
 * To start observing a variable, `cse_observer_start_observing()` must be called.
 */
cse_observer* cse_time_series_observer_create();

/**
 * Creates an observer which buffers up to `bufferSize` variable values in memory.
 *
 * To start observing a variable, `cse_observer_start_observing()` must be called.
 */
cse_observer* cse_buffered_time_series_observer_create(size_t bufferSize);

/// Start observing a variable with a `time_series_observer`.
int cse_observer_start_observing(cse_observer* observer, cse_slave_index slave, cse_variable_type type, cse_variable_index index);

/// Stop observing a variable with a `time_series_observer`.
int cse_observer_stop_observing(cse_observer* observer, cse_slave_index slave, cse_variable_type type, cse_variable_index index);

/// Destroys an observer
int cse_observer_destroy(cse_observer* observer);


/**
 *  Add an observer to an execution.
 *
 *  \param [in] execution
 *      The execution.
 *  \param [in] observer
 *      A pointer to an observer, which may not be null. The observer may
 *      not previously have been added to any execution.
 *
 *  \returns
 *      0 on success and -1 on error.
 */
int cse_execution_add_observer(
    cse_execution* execution,
    cse_observer* observer);

/// Creates a manipulator for overriding variable values
cse_manipulator* cse_override_manipulator_create();

/**
 *  Add a manipulator to an execution.
 *
 *  \param [in] execution
 *      The execution.
 *  \param [in] manipulator
 *      A pointer to a manipulator, which may not be null. The manipulator may
 *      not previously have been added to any execution.
 *
 *  \returns
 *      0 on success and -1 on error.
 */
int cse_execution_add_manipulator(
    cse_execution* execution,
    cse_manipulator* manipulator);

/// Destroys a manipulator
int cse_manipulator_destroy(cse_manipulator* manipulator);

/// Creates a manipulator for running scenarios.
cse_manipulator* cse_scenario_manager_create();

/// Loads and executes a scenario from file.
int cse_execution_load_scenario(
    cse_execution* execution,
    cse_manipulator* manipulator,
    const char* scenarioFile);

/// Checks if a scenario is running
int cse_scenario_is_running(cse_manipulator* manipulator);

/// Aborts the execution of a running scenario
int cse_scenario_abort(cse_manipulator* manipulator);

#ifdef __cplusplus
} // extern(C)
#endif

#endif // header guard
