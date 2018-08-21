#include <stdio.h>
#include <stdlib.h>

#include <cse.h>


void print_last_error()
{
    fprintf(stderr,
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

    cse_execution* execution = cse_execution_create(0.0);
    if (!execution) {
        print_last_error();
        return 1;
    }

    cse_slave_index slave = cse_execution_add_slave_from_fmu(execution, fmuPath);
    if (slave < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    double dt = 0.1;
    for (double t = 0.0; t <= 1.0; t += dt) {
        rc = cse_execution_step(execution, dt);
        if (rc < 0) {
            print_last_error();
            cse_execution_destroy(execution);
            return 1;
        }

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
    }

    rc = cse_execution_destroy(execution);
    if (rc < 0) {
        print_last_error();
        return 1;
    }

    return 0;
}
