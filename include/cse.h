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
typedef double cse_time_duration;

/// Variable index.
typedef uint32_t cse_variable_index;

/// Slave index.
typedef int cse_slave_index;

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
 *  \returns
 *      A pointer to an object which holds the execution state,
 *      or NULL on error.
 */
cse_execution* cse_execution_create(cse_time_point startTime);


/**
 *  \brief
 *  Destroys an execution.
 *
 *  \returns
 *      0 on success and -1 on error.
 */
int cse_execution_destroy(cse_execution* execution);


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
 *  \param [in] fmuPath
 *      The path to the FMU.
 *
 *  \returns
 *      The slave's unique index in the execution, or -1 on error.
 */
cse_slave_index cse_execution_add_slave_from_fmu(
    cse_execution* execution,
    const char* fmuPath);


/**
 *  \brief
 *  Advances an execution one time step.
 *
 *  \returns
 *      0 on success and -1 on error.
 */
int cse_execution_step(cse_execution* execution, cse_time_duration stepSize);

/**
 *  \brief
 *  Sets the values of real variables for one slave.
 *
 *  \param [in] execution
 *      The execution.
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
int cse_execution_slave_set_real(
    cse_execution* execution,
    cse_slave_index slave,
    const cse_variable_index variables[],
    size_t nv,
    const double values[]);

/**
 *  \brief
 *  Retrieves the values of real variables for one slave.
 *
 *  \param [in] execution
 *      The execution.
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
int cse_execution_slave_get_real(
    cse_execution* execution,
    cse_slave_index slave,
    const cse_variable_index variables[],
    size_t nv,
    double values[]);

/**
 *  \brief
 *  Sets the values of integer variables for one slave.
 *
 *  \param [in] execution
 *      The execution.
 *  \param [in] slave
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
    cse_slave_index slave,
    const cse_variable_index variables[],
    size_t nv,
    const int values[]);

/**
 *  \brief
 *  Retrieves the values of integer variables for one slave.
 *
 *  \param [in] execution
 *      The execution.
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
int cse_execution_slave_get_integer(
    cse_execution* execution,
    cse_slave_index slave,
    const cse_variable_index variables[],
    size_t nv,
    int values[]);


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

struct cse_address_s;
typedef struct cse_address_s cse_address;

int cse_address_destroy(cse_address* address);

// Observer
struct cse_observer_s;
typedef struct cse_observer_s cse_observer;

cse_observer* cse_membuffer_observer_create();

int cse_observer_destroy(cse_observer* observer);

cse_address* cse_observer_get_address(cse_observer* observer);


// Slave
struct cse_slave_s;
typedef struct cse_slave_s cse_slave;

cse_slave* cse_local_slave_create(const char* fmuPath);

int cse_slave_destroy(cse_slave* slave);

cse_address* cse_slave_get_address(cse_slave* s);


// Execution
cse_execution* cse_execution_create2( // replaces cse_execution_create()
    cse_time_point startTime,
    cse_time_duration stepSize);

int cse_execution_add_slave( // replaces cse_execution_add_slave_from_fmu()
    cse_execution* execution,
    cse_address* address,
    const char* name);

int cse_execution_add_observer(
    cse_execution* execution,
    cse_address* address,
    const char* name);

int cse_execution_start(cse_execution* execution); // replaces cse_execution_step()

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

double cse_execution_get_status(
    cse_execution* execution,
    cse_execution_status* status);


#ifdef __cplusplus
} // extern(C)
#endif

#endif // header guard
