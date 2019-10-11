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
    cse_observer* observer = NULL;
    cse_manipulator* manipulator = NULL;


    const char* dataDir = getenv("TEST_DATA_DIR");
    if (!dataDir) {
        fprintf(stderr, "Environment variable TEST_DATA_DIR not set\n");
        goto Lfailure;
    }

    char sspDir[1024];
    int rc = snprintf(sspDir, sizeof sspDir, "%s/ssp/controller", dataDir);
    if (rc < 0) {
        perror(NULL);
        goto Lfailure;
    }

    execution = cse_ssp_execution_create(sspDir, false, 0);
    if (!execution) { goto Lerror; }

    observer = cse_last_value_observer_create();
    if (!observer) { goto Lerror; }
    cse_execution_add_observer(execution, observer);

    manipulator = cse_override_manipulator_create();
    if (!manipulator) { goto Lerror; }

    rc = cse_execution_add_manipulator(execution, manipulator);
    if (rc < 0) { goto Lerror; }

    rc = cse_execution_step(execution, 1);
    if (rc < 0) { goto Lerror; }

    rc = cse_execution_start(execution);
    if (rc < 0) { goto Lerror; }

    Sleep(100);

    cse_value_reference ref = 2;
    const bool val = true;
    // Produces a model error in the subsequent step
    rc = cse_manipulator_slave_set_boolean(manipulator, 0, &ref, 1, &val);
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
    cse_observer_destroy(observer);
    cse_manipulator_destroy(manipulator);
    cse_execution_destroy(execution);
    return exitCode;
}
