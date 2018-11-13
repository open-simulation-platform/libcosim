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
    char fmuPath[1024];
    snprintf(fmuPath, sizeof fmuPath, "%s/fmi1/identity.fmu", dataDir);

    cse_execution* execution = cse_execution_create(0.0, 0.1);
    cse_slave* slave1 = cse_local_slave_create(fmuPath);
    cse_slave* slave2 = cse_local_slave_create(fmuPath);
    cse_observer* observer = cse_membuffer_observer_create();

    cse_slave_index slaveIndex1 = cse_execution_add_slave(execution, slave1);
    cse_slave_index slaveIndex2 = cse_execution_add_slave(execution, slave2);

    cse_execution_add_observer(execution, observer);

    int rc = cse_execution_connect_real_variables(execution, slaveIndex1, 0, slaveIndex2, 0);
    if (rc < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    rc = cse_execution_connect_integer_variables(execution, slaveIndex1, 0, slaveIndex2, 0);
    if (rc < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    cse_variable_index realInVar = 0;
    const double realInVal = 5.0;
    cse_execution_slave_set_real(execution, slaveIndex1, &realInVar, 1, &realInVal);

    cse_variable_index intInVar = 0;
    const int intInVal = 42;
    cse_execution_slave_set_integer(execution, slaveIndex1, &intInVar, 1, &intInVal);

    cse_execution_step(execution, 10);

    cse_variable_index realOutVar = 0;
    double realOutVal = -1.0;
    cse_observer_slave_get_real(observer, slaveIndex2, &realOutVar, 1, &realOutVal);

    cse_variable_index intOutVar = 0;
    int intOutVal = -1;
    cse_observer_slave_get_integer(observer, slaveIndex2, &intOutVar, 1, &intOutVal);

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

    rc = cse_observer_destroy(observer);
    if (rc < 0) {
        print_last_error();
        return 1;
    }

    return 0;
}
