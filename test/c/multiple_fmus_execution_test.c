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

    cse_execution* execution = cse_execution_create(0.0, 0.1);
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

    cse_observer* observer1 = cse_membuffer_observer_create();
    if (!observer1) {
        print_last_error();
        return 1;
    }

    cse_observer* observer2 = cse_membuffer_observer_create();
    if (!observer2) {
        print_last_error();
        return 1;
    }

    cse_slave_index slave_index1 = cse_execution_add_slave(execution, slave1);
    if (slave_index1 < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }
    cse_slave_index slave_index2 = cse_execution_add_slave(execution, slave2);
    if (slave_index2 < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    cse_slave_index observer_slave_index1 = cse_observer_add_slave(observer1, slave1);
    if (observer_slave_index1 < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    cse_slave_index observer_slave_index2 = cse_observer_add_slave(observer2, slave2);
    if (observer_slave_index2 < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    cse_observer_index observer_index1 = cse_execution_add_observer(execution, observer1);
    if (observer_index1 < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    cse_observer_index observer_index2 = cse_execution_add_observer(execution, observer2);
    if (observer_index2 < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }


    cse_variable_index realInVar = 0;
    const double realInVal = 5.0;
    rc = cse_execution_slave_set_real(execution, slave_index1, &realInVar, 1, &realInVal);
    if (rc < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    cse_variable_index intInVar = 0;
    const int intInVal = 42;
    rc = cse_execution_slave_set_integer(execution, slave_index1, &intInVar, 1, &intInVal);
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

    if (execution_status.state != CSE_EXECUTION_STOPPED) {
        fprintf(stderr, "Expected state == %i, got %i\n", CSE_EXECUTION_STOPPED, execution_status.state);
        cse_execution_destroy(execution);
        return 1;
    }

    if (execution_status.error_code != CSE_ERRC_SUCCESS) {
        fprintf(stderr, "Expected error code == %i, got %i\n", CSE_ERRC_SUCCESS, execution_status.error_code);
        cse_execution_destroy(execution);
        return 1;
    }

    cse_variable_index realOutVar = 0;
    double realOutVal = -1.0;
    rc = cse_observer_slave_get_real(observer1, observer_slave_index1, &realOutVar, 1, &realOutVal);
    if (rc < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    cse_variable_index intOutVar = 0;
    int intOutVal = 10;
    rc = cse_observer_slave_get_integer(observer1, observer_slave_index1, &intOutVar, 1, &intOutVal);
    if (rc < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    if (realOutVal != 5.0) {
        fprintf(stderr, "Expected value 0.0, got %f\n", realOutVal);
        cse_execution_destroy(execution);
        return 1;
    }
    if (intOutVal != 42) {
        fprintf(stderr, "Expected value 0, got %i\n", intOutVal);
        cse_execution_destroy(execution);
        return 1;
    }

    rc = cse_observer_slave_get_real(observer2, observer_slave_index2, &realOutVar, 1, &realOutVal);
    if (rc < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    rc = cse_observer_slave_get_integer(observer2, observer_slave_index2, &intOutVar, 1, &intOutVal);
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
