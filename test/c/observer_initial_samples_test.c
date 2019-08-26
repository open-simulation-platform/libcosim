#include <cse.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

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
    cse_slave* slave = NULL;
    cse_observer* observer = NULL;
    cse_manipulator* manipulator = NULL;

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

    int64_t nanoStepSize = (int64_t)(0.1 * 1.0e9);
    execution = cse_execution_create(0, nanoStepSize);
    if (!execution) { goto Lerror; }

    slave = cse_local_slave_create(fmuPath);
    if (!slave) { goto Lerror; }

    observer = cse_last_value_observer_create();
    if (!observer) { goto Lerror; }

    cse_slave_index slave_index = cse_execution_add_slave(execution, slave);
    if (slave_index < 0) { goto Lerror; }

    rc = cse_execution_add_observer(execution, observer);
    if (rc < 0) { goto Lerror; }

    manipulator = cse_override_manipulator_create();
    if (!manipulator) { goto Lerror; }

    rc = cse_execution_add_manipulator(execution, manipulator);
    if (rc < 0) { goto Lerror; }

    // ===== Getting real before step

    cse_variable_index realOutVar = 0;
    double realOutVal = -1.0;
    rc = cse_observer_slave_get_real(observer, slave_index, &realOutVar, 1, &realOutVal);
    if (rc < 0) { goto Lerror; }

    double realVal = 1.2;
    rc = cse_manipulator_slave_set_real(manipulator, 0, &realOutVar, 1, &realVal);
    if (rc < 0) { goto Lerror; }

    rc = cse_execution_step(execution, 10);
    if (rc < 0) { goto Lerror; }

    if (realOutVal != 0.0) {
        fprintf(stderr, "Expected value 0.0, got %f\n", realOutVal);
        goto Lfailure;
    }

    goto Lcleanup;

Lerror:
    print_last_error();

Lfailure:
    exitCode = 1;

Lcleanup:
    cse_manipulator_destroy(manipulator);
    cse_observer_destroy(observer);
    cse_local_slave_destroy(slave);
    cse_execution_destroy(execution);

    return exitCode;
}
