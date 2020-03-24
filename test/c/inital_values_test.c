#include <cse.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WINDOWS
#    include <windows.h>
#else
#    include <unistd.h>
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
    cse_slave* slave = NULL;
    cse_observer* observer = NULL;

    const char* dataDir = getenv("TEST_DATA_DIR");
    if (!dataDir) {
        fprintf(stderr, "Environment variable TEST_DATA_DIR not set\n");
        goto Lfailure;
    }

    char fmuPath[1024];
    int rc = snprintf(fmuPath, sizeof fmuPath, "%s/fmi1/identity.fmu", dataDir);
    if (rc < 0) {
        perror(NULL);
        goto Lfailure;
    }

    // ===== Can step n times and get status
    int64_t nanoStepSize = (int64_t)(0.1 * 1.0e9);
    execution = cse_execution_create(0, nanoStepSize);
    if (!execution) { goto Lerror; }

    slave = cse_local_slave_create(fmuPath, "slave");
    if (!slave) { goto Lerror; }

    observer = cse_last_value_observer_create();
    if (!observer) { goto Lerror; }

    cse_slave_index slave_index = cse_execution_add_slave(execution, slave);
    if (slave_index < 0) { goto Lerror; }

    rc = cse_execution_add_observer(execution, observer);
    if (rc < 0) { goto Lerror; }

    cse_value_reference realVr = 0;
    double initialRealVal = 1.2;
    cse_execution_set_real_initial_value(execution, slave_index, realVr, initialRealVal);

    cse_value_reference intVr = 0;
    int initialIntVal = -5;
    cse_execution_set_integer_initial_value(execution, slave_index, intVr, initialIntVal);

    cse_value_reference boolVr = 0;
    int initialBoolVal = true;
    cse_execution_set_boolean_initial_value(execution, slave_index, boolVr, initialBoolVal);

    cse_value_reference strVr = 0;
    char* initialStrVal = "Hello World!";
    cse_execution_set_boolean_initial_value(execution, slave_index, strVr, initialStrVal);

    rc = cse_execution_step(execution, 1);
    if (rc < 0) { goto Lerror; }

    double actualRealVal = -1;
    rc = cse_observer_slave_get_real(observer, slave_index, &realVr, 1, &actualRealVal);
    if (rc < 0) { goto Lerror; }

    if (actualRealVal != initialRealVal) {
        fprintf(stderr, "Expected value %f, got %f\n", initialRealVal, actualRealVal);
        goto Lfailure;
    }

    int actualIntVal = -1;
    rc = cse_observer_slave_get_integer(observer, slave_index, &intVr, 1, &actualIntVal);
    if (rc < 0) { goto Lerror; }

    if (actualIntVal != initialIntVal) {
        fprintf(stderr, "Expected value %i, got %i\n", initialIntVal, actualIntVal);
        goto Lfailure;
    }

    bool actualBoolVal = 0;
    rc = cse_observer_slave_get_boolean(observer, slave_index, &boolVr, 1, &actualBoolVal);
    if (rc < 0) { goto Lerror; }

    if (actualBoolVal != initialBoolVal) {
        fprintf(stderr, "Expected value %i, got %i\n", initialBoolVal, actualBoolVal);
        goto Lfailure;
    }

    const char* actualStrVal = NULL;
    rc = cse_observer_slave_get_string(observer, slave_index, &boolVr, 1, &actualStrVal);
    if (rc < 0) { goto Lerror; }

    if (!strcmp(actualStrVal, initialStrVal)) {
        fprintf(stderr, "Expected value %s, got %s\n", initialStrVal, actualStrVal);
        goto Lfailure;
    }

    goto Lcleanup;

    Lerror:
    print_last_error();

    Lfailure:
    exitCode = 1;

    Lcleanup:
    cse_observer_destroy(observer);
    cse_local_slave_destroy(slave);
    cse_execution_destroy(execution);

    return exitCode;
}
