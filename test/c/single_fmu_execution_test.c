#include <stdio.h>
#include <stdlib.h>

#include <cse.h>
#include <math.h>

#ifdef _WINDOWS
#include <windows.h>
#else
#include <unistd.h>
#define Sleep(x) usleep((x)*1000)
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

    // ===== Can step n times and get status
    cse_execution* execution = cse_execution_create(0.0, 0.1);
    if (!execution) {
        print_last_error();
        return 1;
    }

    cse_slave_index slave = cse_execution_add_slave(execution, 0, fmuPath);
    if (slave < 0) {
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

    cse_execution_status execution_status;
    rc = cse_execution_get_status(execution, &execution_status);
    if (rc < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    double precision = 1e-9;
    if (fabs(execution_status.current_time - 1.0) > precision) {
        fprintf(stderr, "Expected current time == 1.0, got %f\n", execution_status.current_time);
        cse_execution_destroy(execution);
        return 1;
    }

    if (execution_status.state != CSE_EXECUTION_RUNNING) {
        fprintf(stderr, "Expected state == %i, got %i\n",CSE_EXECUTION_RUNNING, execution_status.state);
        cse_execution_destroy(execution);
        return 1;
    }

    if (execution_status.error_code != CSE_ERRC_SUCCESS) {
        fprintf(stderr, "Expected error code == %i, got %i\n",CSE_ERRC_SUCCESS, execution_status.error_code);
        cse_execution_destroy(execution);
        return 1;
    }

    // ===== Can start/stop execution and get status
    cse_variable_index realInVar = 0;
    const double realInVal = 5.0;
    rc = cse_execution_slave_set_real(execution, slave, &realInVar, 1, &realInVal);
    if (rc < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    cse_variable_index intInVar = 0;
    const int intInVal = 42;
    rc = cse_execution_slave_set_integer(execution, slave, &intInVar, 1, &intInVal);
    if (rc < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    rc = cse_execution_start(execution);
    if (rc < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    rc = cse_execution_get_status(execution, &execution_status);
    if (rc < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    if (execution_status.state != CSE_EXECUTION_RUNNING) {
        fprintf(stderr, "Expected state == %i, got %i\n",CSE_EXECUTION_RUNNING, execution_status.state);
        cse_execution_destroy(execution);
        return 1;
    }

    if (execution_status.error_code != CSE_ERRC_SUCCESS) {
        fprintf(stderr, "Expected error code == %i, got %i\n",CSE_ERRC_SUCCESS, execution_status.error_code);
        cse_execution_destroy(execution);
        return 1;
    }

    Sleep(100);

    rc = cse_execution_stop(execution);
    if (rc < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    rc = cse_execution_get_status(execution, &execution_status);
    if (rc < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    if (execution_status.state != CSE_EXECUTION_STOPPED) {
        fprintf(stderr, "Expected state == %i, got %i\n",CSE_EXECUTION_STOPPED, execution_status.state);
        cse_execution_destroy(execution);
        return 1;
    }

    if (execution_status.error_code != CSE_ERRC_SUCCESS) {
        fprintf(stderr, "Expected error code == %i, got %i\n",CSE_ERRC_SUCCESS, execution_status.error_code);
        cse_execution_destroy(execution);
        return 1;
    }

    cse_variable_index realOutVar = 0;
    double realOutVal = -1.0;
    rc = cse_execution_slave_get_real(
        execution, slave,
        &realOutVar, 1, &realOutVal);
    if (rc < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    cse_variable_index intOutVar = 0;
    int intOutVal = 10;
    rc = cse_execution_slave_get_integer(execution, slave, &intOutVar, 1, &intOutVal);
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

    rc = cse_execution_destroy(execution);
    if (rc < 0) {
        print_last_error();
        return 1;
    }

    return 0;
}
