#include <stdio.h>
#include <stdlib.h>

#include <cse.h>
#include <math.h>

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
    if (!slave) {
        print_last_error();
        return 1;
    }

    cse_observer* observer = cse_membuffer_observer_create();
    if (!observer) {
        print_last_error();
        return 1;
    }

    cse_slave_index slave_index = cse_execution_add_slave(execution, slave);
    if (slave_index < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    int observer_index = cse_execution_add_observer(execution, observer);
    if (observer_index < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    double inputRealSamples[10] = {0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9, 1.0};
    int inputIntSamples[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    cse_variable_index index = 0;

    for (int i = 0; i < 10; i++) {
        cse_execution_slave_set_real(execution, 0, &index, 1, &inputRealSamples[i]);
        cse_execution_slave_set_integer(execution, 0, &index, 1, &inputIntSamples[i]);
        cse_execution_step(execution, 1);
    }

    long long fromStep = 0;
    const size_t nSamples = 10;
    double realSamples[10];
    int intSamples[10];
    int64_t times[10];
    long long steps[10];

    size_t readRealSamples = cse_observer_slave_get_real_samples(observer, slave_index, index, fromStep, nSamples, realSamples, steps, times);
    if (readRealSamples != nSamples) {
        print_last_error();
        fprintf(stderr, "Expected to read 10 real samples, got %zu\n", readRealSamples);
        cse_execution_destroy(execution);
        return 1;
    }

    size_t readIntSamples = cse_observer_slave_get_integer_samples(observer, slave_index, index, fromStep, nSamples, intSamples, steps, times);
    if (readIntSamples != nSamples) {
        print_last_error();
        fprintf(stderr, "Expected to read 10 int samples, got %zu\n", readIntSamples);
        cse_execution_destroy(execution);
        return 1;
    }

    long expectedSteps[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    double expectedRealSamples[10] = {0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9};
    int expectedIntSamples[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    double t[10] = {0.0, 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9};
    int64_t expectedTimeSamples[10];
    for (int j = 0; j < 10; ++j) {
        expectedTimeSamples[j] = (int64_t)(1.0e9 * t[j]);
    }


    for (int i = 0; i < 10; i++) {
        if (fabs(expectedRealSamples[i] - realSamples[i]) > 0.000001) {
            fprintf(stderr, "Sample nr %d expected real sample %lf, got %lf\n", i, expectedRealSamples[i], realSamples[i]);
            cse_execution_destroy(execution);
            return 1;
        }
        if (expectedIntSamples[i] != intSamples[i]) {
            fprintf(stderr, "Sample nr %d expected int sample %d, got %d\n", i, expectedIntSamples[i], intSamples[i]);
            cse_execution_destroy(execution);
            return 1;
        }
        if (expectedSteps[i] != steps[i]) {
            fprintf(stderr, "Sample nr %d expected step %li, got %lli\n", i, expectedSteps[i], steps[i]);
            cse_execution_destroy(execution);
            return 1;
        }
        if (expectedTimeSamples[i] != times[i]) {
            fprintf(stderr, "Sample nr %d expected time sample %lli, got %lli\n", i, expectedTimeSamples[i], times[i]);
            cse_execution_destroy(execution);
            return 1;
        }
    }

    cse_step_number nums[2];
    cse_duration dur = (int64_t)(0.5 * 1.0e9);
    cse_observer_get_step_numbers_for_duration(observer, 0, dur, nums);
    if (nums[0] != 5) {
        fprintf(stderr, "Expected step number %i, got %lli\n", 5, nums[0]);
        cse_execution_destroy(execution);
        return 1;
    }
    if (nums[1] != 10) {
        fprintf(stderr, "Expected step number %i, got %lli\n", 10, nums[1]);
        cse_execution_destroy(execution);
        return 1;
    }

    cse_time_point t1 = (int64_t)(0.3 * 1e9);
    cse_time_point t2 = (int64_t)(0.6 * 1e9);
    cse_observer_get_step_numbers(observer, 0, t1, t2, nums);
    if (nums[0] != 3) {
        fprintf(stderr, "Expected step number %i, got %lli\n", 3, nums[0]);
        cse_execution_destroy(execution);
        return 1;
    }
    if (nums[1] != 6) {
        fprintf(stderr, "Expected step number %i, got %lli\n", 6, nums[1]);
        cse_execution_destroy(execution);
        return 1;
    }


    cse_execution_destroy(execution);
    return 0;
}
