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
    const char* dataDir = getenv("TEST_DATA_DIR");
    if (!dataDir) {
        fprintf(stderr, "Environment variable TEST_DATA_DIR not set\n");
        return 1;
    }

    char fmuPath[1024];
    int rc = snprintf(fmuPath, sizeof fmuPath, "%s/fmi1/identity.fmu", dataDir);
    if (rc < 0) {
        perror(NULL);
        return 1;
    }

    int64_t nanoStepSize = (int64_t)(0.1 * 1.0e9);
    cse_execution* execution = cse_execution_create(0, nanoStepSize);
    if (!execution) {
        print_last_error();
        return 1;
    }

    cse_slave* slave = cse_local_slave_create(fmuPath);

    cse_observer* observer = cse_time_series_observer_create();
    if (!observer) {
        print_last_error();
        return 1;
    }

    cse_slave_index slave_index = cse_execution_add_slave(execution, slave);

    rc = cse_execution_add_observer(execution, observer);

    double inputRealSamples[10] = {0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0};
    int inputIntSamples[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    cse_variable_index index = 0;

    cse_observer_start_observing(observer, 0, CSE_INTEGER, index);

    cse_manipulator* manipulator = cse_override_manipulator_create();
    cse_execution_add_manipulator(execution, manipulator);

    for (int i = 0; i < 5; i++) {
        cse_manipulator_slave_set_real(manipulator, 0, &index, 1, &inputRealSamples[i]);
        cse_manipulator_slave_set_integer(manipulator, 0, &index, 1, &inputIntSamples[i]);
        cse_execution_step(execution, 1);
    }

    cse_observer_stop_observing(observer, 0, CSE_INTEGER, index);
    cse_observer_start_observing(observer, 0, CSE_REAL, index);

    for (int i = 5; i < 10; i++) {
        cse_manipulator_slave_set_real(manipulator, 0, &index, 1, &inputRealSamples[i]);
        cse_manipulator_slave_set_integer(manipulator, 0, &index, 1, &inputIntSamples[i]);
        cse_execution_step(execution, 1);
    }

    cse_step_number fromStep = 1;
    const size_t nSamples = 10;
    double realSamples[10];
    int intSamples[10];
    cse_time_point times[10];
    cse_step_number steps[10];

    int64_t readRealSamples = cse_observer_slave_get_real_samples(observer, slave_index, index, fromStep, nSamples, realSamples, steps, times);
    if (readRealSamples != 5) {
        print_last_error();
        fprintf(stderr, "Expected to read 5 real samples, got %" PRId64 "\n", readRealSamples);
        cse_execution_destroy(execution);
        return 1;
    }

    int64_t readIntSamples = cse_observer_slave_get_integer_samples(observer, slave_index, index, fromStep, nSamples, intSamples, steps, times);
    if (readIntSamples != 0) {
        print_last_error();
        fprintf(stderr, "Expected to read 0 int samples, got %" PRId64 "\n", readIntSamples);
        cse_execution_destroy(execution);
        return 1;
    }


    cse_execution_destroy(execution);
    return 0;
}
