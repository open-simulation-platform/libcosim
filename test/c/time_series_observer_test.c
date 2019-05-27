#include <cse.h>

#include <inttypes.h>
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

    observer = cse_time_series_observer_create();
    if (!observer) { goto Lerror; }

    cse_slave_index slaveIndex = cse_execution_add_slave(execution, slave);
    if (slaveIndex < 0) { goto Lerror; }

    rc = cse_execution_add_observer(execution, observer);
    if (rc < 0) { goto Lerror; }

    double inputRealSamples[10] = {0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0};
    int inputIntSamples[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    cse_variable_index index = 0;

    rc = cse_observer_start_observing(observer, 0, CSE_VARIABLE_TYPE_INTEGER, index);
    if (rc < 0) { goto Lerror; }

    manipulator = cse_override_manipulator_create();
    if (!manipulator) { goto Lerror; }

    rc = cse_execution_add_manipulator(execution, manipulator);
    if (rc < 0) { goto Lerror; }

    for (int i = 0; i < 5; i++) {
        rc = cse_manipulator_slave_set_real(manipulator, 0, &index, 1, &inputRealSamples[i]);
        if (rc < 0) { goto Lerror; }
        rc = cse_manipulator_slave_set_integer(manipulator, 0, &index, 1, &inputIntSamples[i]);
        if (rc < 0) { goto Lerror; }
        rc = cse_execution_step(execution, 1);
        if (rc < 0) { goto Lerror; }
    }

    rc = cse_observer_stop_observing(observer, 0, CSE_VARIABLE_TYPE_INTEGER, index);
    if (rc < 0) { goto Lerror; }
    rc = cse_observer_start_observing(observer, 0, CSE_VARIABLE_TYPE_REAL, index);
    if (rc < 0) { goto Lerror; }

    for (int i = 5; i < 10; i++) {
        rc = cse_manipulator_slave_set_real(manipulator, 0, &index, 1, &inputRealSamples[i]);
        if (rc < 0) { goto Lerror; }
        rc = cse_manipulator_slave_set_integer(manipulator, 0, &index, 1, &inputIntSamples[i]);
        if (rc < 0) { goto Lerror; }
        rc = cse_execution_step(execution, 1);
        if (rc < 0) { goto Lerror; }
    }

    cse_step_number fromStep = 1;
    const size_t nSamples = 10;
    double realSamples[10];
    int intSamples[10];
    cse_time_point times[10];
    cse_step_number steps[10];

    int64_t readRealSamples = cse_observer_slave_get_real_samples(observer, slaveIndex, index, fromStep, nSamples, realSamples, steps, times);
    if (readRealSamples != 5) {
        fprintf(stderr, "Expected to read 5 real samples, got %" PRId64 "\n", readRealSamples);
        goto Lfailure;
    }

    int64_t readIntSamples = cse_observer_slave_get_integer_samples(observer, slaveIndex, index, fromStep, nSamples, intSamples, steps, times);
    if (readIntSamples != 0) {
        fprintf(stderr, "Expected to read 0 int samples, got %" PRId64 "\n", readIntSamples);
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
