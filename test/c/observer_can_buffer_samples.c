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
    int rc = snprintf(fmuPath, sizeof fmuPath, "%s/fmi2/Clock.fmu", dataDir);
    if (rc < 0) {
        perror(NULL);
        return 1;
    }

    cse_execution* execution = cse_execution_create(0.0, 0.1);
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

    cse_slave_index observer_slave_index = cse_observer_add_slave(observer, slave);
    if (observer_slave_index < 0) {
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

    rc = cse_execution_step(execution, 10);
    if (rc < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    cse_variable_index realOutVar = 0;
    long fromStep = 0;
    const size_t nSamples = 10;
    double realSamples[10];
    long steps[10];
    size_t readSamples = cse_observer_slave_get_real_samples(observer, slave_index, realOutVar, fromStep, nSamples, realSamples, steps);

    double expectedSamples[10] = {0.0 , 0.1, 0.2, 0.3, 0.4, 0.5, 0.6, 0.7, 0.8, 0.9};
    long expectedSteps[10] = {1, 2, 5, 5, 5, 5, 5, 5, 5, 5};

    for (int i = 0; i < 10; i++) {
        if (expectedSamples[i] != realSamples[i]) {
            fprintf(stderr, "Sample nr %d expected sample %lf, got %lf\n", i, expectedSamples[i], realSamples[i]);
        }
        if (expectedSteps[i] != steps[i]) {
            fprintf(stderr, "Sample nr %d expected step %li, got %li\n", i, expectedSteps[i], steps[i]);
        }
    }

    (void)expectedSteps;
    if (readSamples != nSamples) {
        print_last_error();
        fprintf(stderr, "Expected to read 10 samples, got %zu\n", readSamples);
        cse_execution_destroy(execution);
        return 1;
    }

    return 0;
}
