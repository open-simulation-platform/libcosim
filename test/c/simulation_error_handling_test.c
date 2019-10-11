#include <cse.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WINDOWS
#    include <windows.h>
#else
#    include <unistd.h>
#    define Sleep(x) usleep((x)*1000)
#endif

void print_last_error()
{
    fprintf(
        stderr,
        "Error code %d: %s\n",
        cse_last_error_code(), cse_last_error_message());
}

int main()
{
    cse_log_setup_simple_console_logging();
    cse_log_set_output_level(CSE_LOG_SEVERITY_INFO);

    int exitCode = 0;
    cse_execution* execution = NULL;
    cse_manipulator* manipulator = NULL;
    cse_slave* slave = NULL;

    const char* dataDir = getenv("TEST_DATA_DIR");
    if (!dataDir) {
        fprintf(stderr, "Environment variable TEST_DATA_DIR not set\n");
        goto Lfailure;
    }

    char fmuPath[1024];
    int rc = snprintf(fmuPath, sizeof fmuPath, "%s/fmi2/fail.fmu", dataDir);
    if (rc < 0) {
        perror(NULL);
        goto Lfailure;
    }

    int64_t nanoStepSize = (int64_t)(0.1 * 1.0e9);
    execution = cse_execution_create(0, nanoStepSize);
    if (!execution) { goto Lerror; }

    slave = cse_local_slave_create(fmuPath, "slave");
    if (!slave) { goto Lerror; }

    cse_slave_index slave_index = cse_execution_add_slave(execution, slave);
    if (slave_index < 0) { goto Lerror; }

    manipulator = cse_override_manipulator_create();
    if (!manipulator) { goto Lerror; }

    rc = cse_execution_add_manipulator(execution, manipulator);
    if (rc < 0) { goto Lerror; }

    rc = cse_execution_step(execution, 1);
    if (rc < 0) { goto Lerror; }

    rc = cse_execution_start(execution);
    if (rc < 0) { goto Lerror; }

    Sleep(100);

    cse_value_reference ref = 0;
    const bool val = true;
    // Produces a model error in the subsequent step
    rc = cse_manipulator_slave_set_boolean(manipulator, slave_index, &ref, 1, &val);
    if (rc < 0) { goto Lerror; }

    // Need to wait a bit due to stepping (and failure) happening in another thread.
    Sleep(400);

    cse_execution_status executionStatus;
    rc = cse_execution_get_status(execution, &executionStatus);
    if (rc < 0) { goto Lerror; }

    if (executionStatus.state != CSE_EXECUTION_ERROR) {
        fprintf(stderr, "Expected state == %i, got %i\n", CSE_EXECUTION_ERROR, executionStatus.state);
        goto Lfailure;
    }

    print_last_error();
    const char* lastErrorMessage = cse_last_error_message();
    if (0 == strncmp(lastErrorMessage, "", 1)) {
        fprintf(stdout, "Expected to find an error message, but last error was: %s\n", lastErrorMessage);
        goto Lfailure;
    }
    int lastErrorCode = cse_last_error_code();
    if (lastErrorCode != CSE_ERRC_SIMULATION_ERROR) {
        fprintf(stdout, "Expected to find error code %i, but got error code: %i\n", CSE_ERRC_SIMULATION_ERROR, lastErrorCode);
        goto Lfailure;
    }

    // What do we expect should happen if calling further methods?
    //    Sleep(100);
    //    rc = cse_execution_stop(execution);
    //    if (rc = 0) { goto Lfailure; }

    goto Lcleanup;

Lerror:
    print_last_error();

Lfailure:
    exitCode = 1;

Lcleanup:
    cse_manipulator_destroy(manipulator);
    cse_local_slave_destroy(slave);
    cse_execution_destroy(execution);
    return exitCode;
}
