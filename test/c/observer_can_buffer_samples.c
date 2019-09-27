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

    slave = cse_local_slave_create(fmuPath, "slave");
    if (!slave) { goto Lerror; }

    observer = cse_time_series_observer_create();
    if (!observer) { goto Lerror; }

    cse_slave_index slaveIndex = cse_execution_add_slave(execution, slave);
    if (slaveIndex < 0) { goto Lerror; }

    rc = cse_execution_add_observer(execution, observer);
    if (rc < 0) { goto Lerror; }

    manipulator = cse_override_manipulator_create();
    if (!manipulator) { goto Lerror; }

    rc = cse_execution_add_manipulator(execution, manipulator);
    if (rc < 0) { goto Lerror; }

    double inputRealSamples[10] = {0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0};
    int inputIntSamples[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    cse_value_reference reference = 0;

    rc = cse_observer_start_observing(observer, slaveIndex, CSE_VARIABLE_TYPE_REAL, reference);
    if (rc < 0) { goto Lerror; }
    rc = cse_observer_start_observing(observer, slaveIndex, CSE_VARIABLE_TYPE_INTEGER, reference);
    if (rc < 0) { goto Lerror; }

    for (int i = 0; i < 10; i++) {
        rc = cse_manipulator_slave_set_real(manipulator, 0, &reference, 1, &inputRealSamples[i]);
        if (rc < 0) { goto Lerror; }
        rc = cse_manipulator_slave_set_integer(manipulator, 0, &reference, 1, &inputIntSamples[i]);
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

    int64_t readRealSamples = cse_observer_slave_get_real_samples(observer, slaveIndex, reference, fromStep, nSamples, realSamples, steps, times);
    if (readRealSamples != (int64_t)nSamples) {
        print_last_error();
        fprintf(stderr, "Expected to read 10 real samples, got %" PRId64 "\n", readRealSamples);
        goto Lfailure;
    }

    int64_t readIntSamples = cse_observer_slave_get_integer_samples(observer, slaveIndex, reference, fromStep, nSamples, intSamples, steps, times);
    if (readIntSamples != (int64_t)nSamples) {
        print_last_error();
        fprintf(stderr, "Expected to read 10 int samples, got %" PRId64 "\n", readIntSamples);
        goto Lfailure;
    }

    long expectedSteps[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    double expectedRealSamples[10] = {0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0};
    int expectedIntSamples[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    double t[10] = {0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0};
    cse_time_point expectedTimeSamples[10];
    for (int j = 0; j < 10; ++j) {
        expectedTimeSamples[j] = (cse_time_point)(1.0e9 * t[j]);
    }

    for (int i = 0; i < 10; i++) {
        if (fabs(expectedRealSamples[i] - realSamples[i]) > 0.000001) {
            fprintf(stderr, "Sample nr %d expected real sample %lf, got %lf\n", i, expectedRealSamples[i], realSamples[i]);
            goto Lfailure;
        }
        if (expectedIntSamples[i] != intSamples[i]) {
            fprintf(stderr, "Sample nr %d expected int sample %d, got %d\n", i, expectedIntSamples[i], intSamples[i]);
            goto Lfailure;
        }
        if (expectedSteps[i] != steps[i]) {
            fprintf(stderr, "Sample nr %d expected step %li, got %lli\n", i, expectedSteps[i], steps[i]);
            goto Lfailure;
        }
        if (expectedTimeSamples[i] != times[i]) {
            fprintf(stderr, "Sample nr %d expected time sample %" PRId64 ", got %" PRId64 "\n", i, expectedTimeSamples[i], times[i]);
            goto Lfailure;
        }
    }

    cse_step_number nums[2];
    cse_duration dur = (cse_time_point)(0.5 * 1.0e9);
    rc = cse_observer_get_step_numbers_for_duration(observer, 0, dur, nums);
    if (rc < 0) { goto Lerror; }
    if (nums[0] != 5) {
        fprintf(stderr, "Expected step number %i, got %lli\n", 5, nums[0]);
        goto Lfailure;
    }
    if (nums[1] != 10) {
        fprintf(stderr, "Expected step number %i, got %lli\n", 10, nums[1]);
        goto Lfailure;
    }

    cse_time_point t1 = (cse_time_point)(0.3 * 1e9);
    cse_time_point t2 = (cse_time_point)(0.6 * 1e9);
    rc = cse_observer_get_step_numbers(observer, 0, t1, t2, nums);
    if (rc < 0) { goto Lerror; }
    if (nums[0] != 3) {
        fprintf(stderr, "Expected step number %i, got %lli\n", 3, nums[0]);
        goto Lfailure;
    }
    if (nums[1] != 6) {
        fprintf(stderr, "Expected step number %i, got %lli\n", 6, nums[1]);
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
