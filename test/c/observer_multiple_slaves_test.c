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

    cse_slave* slave1 = cse_local_slave_create(fmuPath);
    if (!slave1) {
        print_last_error();
        return 1;
    }
    cse_slave* slave2 = cse_local_slave_create(fmuPath);
    if (!slave2) {
        print_last_error();
        return 1;
    }

    cse_observer* observer = cse_last_value_observer_create();
    if (!observer) {
        print_last_error();
        return 1;
    }

    cse_slave_index slaveIndex1 = cse_execution_add_slave(execution, slave1);
    if (slaveIndex1 < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }
    cse_slave_index slaveIndex2 = cse_execution_add_slave(execution, slave2);
    if (slaveIndex2 < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    rc = cse_execution_add_observer(execution, observer);
    if (rc < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    cse_manipulator* manipulator = cse_override_manipulator_create();
    if (!manipulator) {
        print_last_error();
        return 1;
    }

    rc = cse_execution_add_manipulator(execution, manipulator);
    if (rc < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    cse_variable_index realInVar = 0;
    const double realInVal = 5.0;
    rc = cse_manipulator_slave_set_real(manipulator, slaveIndex1, &realInVar, 1, &realInVal);
    if (rc < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    cse_variable_index intInVar = 0;
    const int intInVal = 42;
    rc = cse_manipulator_slave_set_integer(manipulator, slaveIndex1, &intInVar, 1, &intInVal);
    if (rc < 0) {
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

    cse_execution_status executionStatus;
    rc = cse_execution_get_status(execution, &executionStatus);
    if (rc < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    double precision = 1e-9;
    double simTime = executionStatus.current_time * 1e-9;
    if (fabs(simTime - 1.0) > precision) {
        fprintf(stderr, "Expected current time == 1.0s, got %f\n", simTime);
        cse_execution_destroy(execution);
        return 1;
    }

    if (executionStatus.state != CSE_EXECUTION_STOPPED) {
        fprintf(stderr, "Expected state == %i, got %i\n", CSE_EXECUTION_STOPPED, executionStatus.state);
        cse_execution_destroy(execution);
        return 1;
    }

    if (executionStatus.error_code != CSE_ERRC_SUCCESS) {
        fprintf(stderr, "Expected error code == %i, got %i\n", CSE_ERRC_SUCCESS, executionStatus.error_code);
        cse_execution_destroy(execution);
        return 1;
    }

    cse_variable_index realOutVar = 0;
    double realOutVal = -1.0;
    rc = cse_observer_slave_get_real(observer, slaveIndex1, &realOutVar, 1, &realOutVal);
    if (rc < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    cse_variable_index intOutVar = 0;
    int intOutVal = 10;
    rc = cse_observer_slave_get_integer(observer, slaveIndex1, &intOutVar, 1, &intOutVal);
    if (rc < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    if (realOutVal != 5.0) {
        fprintf(stderr, "Expected value 5.0, got %f\n", realOutVal);
        cse_execution_destroy(execution);
        return 1;
    }
    if (intOutVal != 42) {
        fprintf(stderr, "Expected value 42, got %i\n", intOutVal);
        cse_execution_destroy(execution);
        return 1;
    }

    rc = cse_observer_slave_get_real(observer, slaveIndex2, &realOutVar, 1, &realOutVal);
    if (rc < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    rc = cse_observer_slave_get_integer(observer, slaveIndex2, &intOutVar, 1, &intOutVal);
    if (rc < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    if (realOutVal != 0.0) {
        fprintf(stderr, "Expected value 0.0, got %f\n", realOutVal);
        cse_execution_destroy(execution);
        return 1;
    }
    if (intOutVal != 0) {
        fprintf(stderr, "Expected value 0, got %i\n", intOutVal);
        cse_execution_destroy(execution);
        return 1;
    }

    rc = cse_execution_destroy(execution);
    if (rc < 0) {
        print_last_error();
        return 1;
    }

    return 0;
}
