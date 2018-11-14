//
// Created by STENBRO on 10/9/2018.
//
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

int teardown(cse_execution* execution)
{
    print_last_error();
    cse_execution_destroy(execution);

    return 1;
}

int teardown_message(cse_execution* execution, char* message)
{
    print_last_error();
    fprintf(stderr, "Message: %s\n", message);
    cse_execution_destroy(execution);

    return 1;
}

void info_message(char* message)
{
    fprintf(stderr, "INFO: %s\n", message);
}

int main()
{
    const char* dataDir = getenv("TEST_DATA_DIR");
    char fmuPath[1024];

    int rc = snprintf(fmuPath, sizeof fmuPath, "%s/fmi1/identity.fmu", dataDir);
    if (rc < 0) {
        perror(NULL);
        return 1;
    }

    cse_execution* execution = cse_execution_create(0.0, 0.1);
    cse_slave* slave = cse_local_slave_create(fmuPath);

    cse_observer* observer = cse_membuffer_observer_create();

    cse_slave_index slaveIndex = cse_execution_add_slave(execution, slave);
    if (slaveIndex < 0) {
        teardown(execution);
    }

    cse_slave_index observerSlaveIndex = cse_observer_add_slave(observer, slave);
    if (observerSlaveIndex < 0) {
        teardown(execution);
    }

    cse_observer_index observerIndex = cse_execution_add_observer(execution, observer);
    if (observerIndex < 0) {
        teardown(execution);
    }

    cse_variable_index variable_index = 0;
    const size_t numSamples = 20;
    const long fromStep = 0;

    rc = cse_execution_step(execution, 20);
    if (rc < 0) {
        teardown(execution);
    }

    cse_execution_status execution_status;
    rc = cse_execution_get_status(execution, &execution_status);
    if (rc < 0) {
        teardown(execution);
    }

    rc = cse_execution_start(execution);
    if (rc < 0) {
        teardown(execution);
    }

    Sleep(200);

    rc = cse_execution_stop(execution);
    if (rc < 0) {
        teardown(execution);
    }

    int intValues[20];
    long steps[20];
    size_t intSamples = cse_observer_slave_get_integer_samples(observer, observerSlaveIndex, variable_index, fromStep, numSamples, intValues, steps);

    if (intSamples != 20) {
        teardown_message(execution, "Expected 20 integer samples read");
    }

    double realValues[20];
    size_t realSamples = cse_observer_slave_get_real_samples(observer, observerSlaveIndex, variable_index, fromStep, numSamples, realValues, steps);

    // C api for updated file logger not yet in use
    (void)realSamples;

    return 0;
}
