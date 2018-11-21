/**
 *  \file
 *  C library header.
 *  \ingroup csecorec
 */
#ifndef CSE_H
#define CSE_H

#include <stddef.h>
#include <stdint.h>


/// \defgroup csecorec C library

#ifdef __cplusplus
extern "C" {
#endif


/// The type used to specify (simulation) time points.
typedef double cse_time_point;

/// The type used to specify (simulation) time durations.
typedef double cse_duration;

/// Variable index.
typedef uint32_t cse_variable_index;

/// Slave index.
typedef int cse_slave_index;

/// Observer index.
typedef int cse_observer_index;

/// Step number
typedef long long cse_step_number;

/// Error codes.
typedef enum
{
    CSE_ERRC_SUCCESS = 0,

    // --- Codes unique to the C API ---

    /// Unspecified error (but message may contain details).
    CSE_ERRC_UNSPECIFIED,

    /// Error reported by C/C++ runtime; check `errno` to get the right code.
    CSE_ERRC_ERRNO,

    /// Invalid function argument.
    CSE_ERRC_INVALID_ARGUMENT,

    /// Index out of range.
    CSE_ERRC_OUT_OF_RANGE,

    /**
     *  \brief
     *  The time step failed, but can be retried with a shorter step length
     *  (if supported by all slaves).
     */
    CSE_ERRC_STEP_TOO_LONG,

    // --- Codes that correspond to C++ API error conditions ---

    /// An input file is corrupted or invalid.
    CSE_ERRC_BAD_FILE,

    /// The requested feature (e.g. an FMI feature) is unsupported.
    CSE_ERRC_UNSUPPORTED_FEATURE,

    /// Error loading dynamic library (e.g. model code).
    CSE_ERRC_DL_LOAD_ERROR,

    /// The model reported an error.
    CSE_ERRC_MODEL_ERROR,

    /// ZIP file error.
    CSE_ERRC_ZIP_ERROR,
} cse_errc;


/**
 *  \brief
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
 *  \brief
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


struct cse_execution_s;

/// An opaque object which contains the state for an execution.
typedef struct cse_execution_s cse_execution;


/**
 *  \brief
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
 *  \brief
 *  Destroys an execution.
 *
 *  \returns
 *      0 on success and -1 on error.
 */
int cse_execution_destroy(cse_execution* execution);


struct cse_address_s;
typedef struct cse_address_s cse_address;


int cse_address_destroy(cse_address* address);


struct cse_slave_s;
typedef struct cse_slave_s cse_slave;


/**
 *  \brief
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
 *  \brief
 *  Loads a co-simulation FMU, instantiates a slave based on it, and adds it
 *  to an execution.
 *
 *  The slave is owned by the execution and is destroyed along with it.
 *
 *  \note
 *      Currently (and of course temporarily), only one slave may be added to
 *      an execution.
 *
 *  \param [in] execution
 *      The execution to which the slave should be added.
 *  \param [in] slave
 *      The slave.
 *
 *  \returns
 *      The slave's unique index in the execution, or -1 on error.
 */
cse_slave_index cse_execution_add_slave(
    cse_execution* execution,
    cse_slave* slave);


/**
 *  \brief
 *  Advances an execution one time step.
 *
 *  \param [in] execution
 *      The execution to be stepped.
 *  \param [in] numSteps
 *      The number of steps to advance the execution to which the slave should be added.
 *
 *  \returns
 *      0 on success and -1 on error.
 */
int cse_execution_step(cse_execution* execution, size_t numSteps);


/**
 *  \brief
 *  Starts an execution.
 *
 *  \param [in] execution
 *      The execution to be started.
 *
 *  \returns
 *      0 on success and -1 on error.
 */
int cse_execution_start(cse_execution* execution);


/**
 *  \brief
 *  Stops an execution.
 *
 *  \param [in] execution
 *      The execution to be stopped.
 *
 *  \returns
 *      0 on success and -1 on error.
 */
int cse_execution_stop(cse_execution* execution);


typedef enum
{
    CSE_EXECUTION_STOPPED,
    CSE_EXECUTION_RUNNING,
    CSE_EXECUTION_ERROR
} cse_execution_state;

typedef struct
{
    double current_time;
    cse_execution_state state;
    int error_code;
} cse_execution_status;


/**
 *  \brief
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


// Observer
struct cse_observer_s;
typedef struct cse_observer_s cse_observer;


/**
 *  \brief
 *  Sets the values of real variables for one slave.
 *
 *  \param [in] execution
 *      The execution.
 *  \param [in] slaveIndex
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
int cse_execution_slave_set_real(
    cse_execution* execution,
    cse_slave_index slaveIndex,
    const cse_variable_index variables[],
    size_t nv,
    const double values[]);

/**
 *  \brief
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
 * \brief
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
 * Returns the number of samples actually read, which may be smaller
 * than `nSamples`.
 */
size_t cse_observer_slave_get_real_samples(
    cse_observer* observer,
    cse_slave_index slave,
    cse_variable_index variableIndex,
    cse_step_number fromStep,
    size_t nSamples,
    double values[],
    cse_step_number steps[],
    double times[]);

/**
 *  \brief
 *  Sets the values of integer variables for one slave.
 *
 *  \param [in] execution
 *      The execution.
 *  \param [in] slaveIndex
 *      The slave.
 *  \param [in] variables
 *      A pointer to an array of length `nv` that contains the (slave-specific)
 *      indices of variables to set.
 *  \param [in] nv
 *      The length of the `variables` and `values` arrays.
 *  \param [out] values
 *      A pointer to an array of length `nv` which will be filled with the
 *      values of the variables specified in `variables`, in the same order.
 *
 *  \returns
 *      0 on success and -1 on error.
 */
int cse_execution_slave_set_integer(
    cse_execution* execution,
    cse_slave_index slaveIndex,
    const cse_variable_index variables[],
    size_t nv,
    const int values[]);

/**
 *  \brief
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
 * \brief
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
 * Returns the number of samples actually read, which may be smaller
 * than `nSamples`.
 */
size_t cse_observer_slave_get_integer_samples(
    cse_observer* observer,
    cse_slave_index slave,
    cse_variable_index variableIndex,
    cse_step_number fromStep,
    size_t nSamples,
    int values[],
    cse_step_number steps[],
    double times[]);

/**
 * \brief
 * Retrieves a series of observed simulation times with corresponding step numbers.
 *
 * \param [in] observer the observer
 * \param [in] slave index of the slave
 * \param [in] fromStep the step number to start from
 * \param [in] nSamples the number of samples to read
 * \param [out] values the series of observed time stamps
 * \param [out] steps the corresponding step numbers
 *
 * Returns the number of samples actually read, which may be smaller
 * than the sizes of `values` and `steps`.
 */
size_t cse_observer_slave_get_time_samples(
    cse_observer* observer,
    cse_slave_index slave,
    cse_step_number fromStep,
    size_t nSamples,
    double values[],
    cse_step_number steps[]);

/**
 * \brief
 * Retrieves the step numbers for a range given by a duration.
 *
 * Helper function which can be used in conjunction with `cse_observer_slave_get_xxx_samples()`
 * when it is desired to retrieve the latest available samples given a certain duration.
 *
 * \param [in] observer the observer
 * \param [in] sim index of the slave
 * \param [in] duration the duration to get step numbers for
 * \param [out] steps the corresponding step numbers
 */
int cse_observer_get_step_numbers_for_duration(
    cse_observer* observer,
    cse_slave_index slave,
    double duration,
    cse_step_number steps[]);

/**
 * Retrieves the step numbers for a range given by two points in time.
 *
 * Helper function which can be used in conjunction with `cse_observer_slave_get_xxx_samples()`
 * when it is desired to retrieve samples between two points in time.
 *
 * \param [in] observer the observer
 * \param [in] sim index of the simulator
 * \param [in] tBegin the start of the range
 * \param [in] tEnd the end of the range
 * \param [out] steps the corresponding step numbers
 */
int cse_observer_get_step_numbers(
    cse_observer* observer,
    cse_slave_index slave,
    double begin,
    double end,
    cse_step_number steps[]);

/**
 *  \brief
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
 *  \brief
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

/**
 *  Writes the string "Hello World!" to a character buffer.
 *
 *  The size of `buffer` must be at least `size`. If it is less than 13 bytes,
 *  the string will be truncated.  The string will always be terminated by a
 *  zero byte, even if it is truncated.
 *
 *  \param buffer
 *      A buffer to hold the friendly greeting.
 *  \param size
 *      The size of the buffer.
 *
 *  \returns
 *      The answer to the ultimate question about life, the universe and
 *      everything, for no apparent reason.
 */
int cse_hello_world(char* buffer, size_t size);


cse_observer* cse_membuffer_observer_create();

int cse_observer_destroy(cse_observer* observer);

cse_address* cse_observer_get_address(cse_observer* observer);

int cse_slave_destroy(cse_slave* slave);

cse_address* cse_slave_get_address(cse_slave* s);

cse_observer_index cse_execution_add_observer(
    cse_execution* execution,
    cse_observer* observer);

#ifdef __cplusplus
} // extern(C)
#endif

#endif // header guard
