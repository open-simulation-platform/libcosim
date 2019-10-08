#include <cse.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    int exitCode = 0;

    cse_execution* execution = NULL;
    cse_slave* slave = NULL;

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

    // ===== Can step n times and get status
    int64_t nanoStepSize = (int64_t)(0.1 * 1.0e9);
    execution = cse_execution_create(0, nanoStepSize);
    if (!execution) { goto Lerror; }

    slave = cse_local_slave_create(fmuPath, "slave");
    if (!slave) { goto Lerror; }

    cse_slave_index slaveIndex = cse_execution_add_slave(execution, slave);
    if (slaveIndex < 0) { goto Lerror; }

    size_t nVar = cse_slave_get_num_variables(execution, slaveIndex);
    if (nVar != 8) {
        fprintf(stderr, "Expected 8 variables, got %zu\n", nVar);
        goto Lfailure;
    }

    cse_variable_description vd[8];

    rc = cse_slave_get_variables(execution, slaveIndex, &vd[0], nVar);
    if (rc < 0) {
        print_last_error();
        goto Lerror;
    }

    for (size_t i = 0; i < nVar; i++) {
        if (0 == strncmp(vd[i].name, "stringOut", SLAVE_NAME_MAX_SIZE)) {
            if (vd[i].causality != CSE_VARIABLE_CAUSALITY_OUTPUT) {
                fprintf(stderr, "Expected causality to be output\n");
                goto Lfailure;
            }
            if (vd[i].variability != CSE_VARIABLE_VARIABILITY_DISCRETE) {
                fprintf(stderr, "Expected variability to be discrete\n");
                goto Lfailure;
            }
            if (vd[i].type != CSE_VARIABLE_TYPE_STRING) {
                fprintf(stderr, "Expected type to be string\n");
                goto Lfailure;
            }
            if (vd[i].reference != 0) {
                fprintf(stderr, "Expected variable reference to be 0, got %i\n", vd[i].reference);
                goto Lfailure;
            }
        }
        if (0 == strncmp(vd[i].name, "realIn", SLAVE_NAME_MAX_SIZE)) {
            if (vd[i].causality != CSE_VARIABLE_CAUSALITY_INPUT) {
                fprintf(stderr, "Expected causality to be input\n");
                goto Lfailure;
            }
            if (vd[i].variability != CSE_VARIABLE_VARIABILITY_DISCRETE) {
                fprintf(stderr, "Expected variability to be discrete\n");
                goto Lfailure;
            }
            if (vd[i].type != CSE_VARIABLE_TYPE_REAL) {
                fprintf(stderr, "Expected type to be real\n");
                goto Lfailure;
            }
            if (vd[i].reference != 0) {
                fprintf(stderr, "Expected variable reference to be 0, got %i\n", vd[i].reference);
                goto Lfailure;
            }
        }
    }


    goto Lcleanup;

Lerror:
    print_last_error();

Lfailure:
    exitCode = 1;

Lcleanup:
    cse_local_slave_destroy(slave);
    cse_execution_destroy(execution);

    return exitCode;
}
