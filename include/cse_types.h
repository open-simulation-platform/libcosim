/**
 *  \file
 *  C library header.
 *  \ingroup csecorec
 */
#ifndef CSE_TYPES_H
#define CSE_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/// \defgroup csecorec C library types

#ifdef __cplusplus
extern "C" {
#endif


/// The type used to specify (simulation) time points. The time unit is nanoseconds.
typedef int64_t cse_time_point;

/// The type used to specify (simulation) time durations. The time unit is nanoseconds.
typedef int64_t cse_duration;

/// Variable index.
typedef uint32_t cse_variable_index;

/// Slave index.
typedef int cse_slave_index;

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


struct cse_execution_s;

/// An opaque object which contains the state for an execution.
typedef struct cse_execution_s cse_execution;

struct cse_slave_s;

/// An opaque object which contains the state for a slave.
typedef struct cse_slave_s cse_slave;

/// Execution states.
typedef enum
{
    CSE_EXECUTION_STOPPED,
    CSE_EXECUTION_RUNNING,
    CSE_EXECUTION_ERROR
} cse_execution_state;

/// A struct containing the execution status.
typedef struct
{
    /// Current simulation time.
    cse_time_point current_time;
    /// Current execution state.
    cse_execution_state state;
    /// Last recorded error code.
    int error_code;
    /// Current real time factor.
    double real_time_factor;
    /// Current real time factor target.
    double real_time_factor_target;
    /// Executing towards real time target.
    int is_real_time_simulation;
} cse_execution_status;

/// Max number of characters used for slave name and source.
#define SLAVE_NAME_MAX_SIZE 1024

/// Variable types.
typedef enum
{
    CSE_VARIABLE_TYPE_REAL,
    CSE_VARIABLE_TYPE_INTEGER,
    CSE_VARIABLE_TYPE_STRING,
    CSE_VARIABLE_TYPE_BOOLEAN,
} cse_variable_type;

/// Variable causalities.
typedef enum
{
    CSE_VARIABLE_CAUSALITY_INPUT,
    CSE_VARIABLE_CAUSALITY_PARAMETER,
    CSE_VARIABLE_CAUSALITY_OUTPUT,
    CSE_VARIABLE_CAUSALITY_CALCULATEDPARAMETER,
    CSE_VARIABLE_CAUSALITY_LOCAL,
    CSE_VARIABLE_CAUSALITY_INDEPENDENT
} cse_variable_causality;

/// Variable variabilities.
typedef enum
{
    CSE_VARIABLE_VARIABILITY_CONSTANT,
    CSE_VARIABLE_VARIABILITY_FIXED,
    CSE_VARIABLE_VARIABILITY_TUNABLE,
    CSE_VARIABLE_VARIABILITY_DISCRETE,
    CSE_VARIABLE_VARIABILITY_CONTINUOUS
} cse_variable_variability;

/// A struct containing metadata for a variable.
typedef struct
{
    /// The name of the variable.
    char name[SLAVE_NAME_MAX_SIZE];
    /// The variable index.
    cse_variable_index index;
    /// The variable type.
    cse_variable_type type;
    /// The variable causality.
    cse_variable_causality causality;
    /// The variable variability.
    cse_variable_variability variability;
} cse_variable_description;


/// A struct containing information about a slave which has been added to an execution.
typedef struct
{
    /// The slave instance name.
    char name[SLAVE_NAME_MAX_SIZE];
    /// The slave source (FMU file name).
    char source[SLAVE_NAME_MAX_SIZE];
    /// The slave's unique index in the exeuction.
    cse_slave_index index;
} cse_slave_info;


// Observer
struct cse_observer_s;

/// An opaque object which contains the state for an observer.
typedef struct cse_observer_s cse_observer;

// Manipulator
struct cse_manipulator_s;

/// An opaque object which contains the state for a manipulator.
typedef struct cse_manipulator_s cse_manipulator;


#ifdef __cplusplus
} // extern(C)
#endif

#endif // header guard
