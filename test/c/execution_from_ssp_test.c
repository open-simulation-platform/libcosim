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
    const char* dataDir = getenv("TEST_DATA_DIR");
    if (!dataDir) {
        fprintf(stderr, "Environment variable TEST_DATA_DIR not set\n");
        return 1;
    }

    char sspDir[1024];
    int rc = snprintf(sspDir, sizeof sspDir, "%s/ssp/demo", dataDir);
    if (rc < 0) {
        perror(NULL);
        return 1;
    }

    cse_execution* execution = cse_ssp_execution_create(sspDir, 0);
    if (!execution) {
        print_last_error();
        return 1;
    }

    cse_observer* observer = cse_membuffer_observer_create();
    cse_execution_add_observer(execution, observer);

    rc = cse_execution_step(execution, 3);
    if (rc < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    size_t numSlaves = cse_execution_get_num_slaves(execution);

    cse_slave_info infos[2];
    rc = cse_execution_get_slave_infos(execution, &infos[0], numSlaves);
    if (rc < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    for (size_t i = 0; i < numSlaves; i++) {
        if (0 == strncmp(infos[i].name, "KnuckleBoomCrane", SLAVE_NAME_MAX_SIZE)) {
            double value = -1;
            cse_slave_index slaveIndex = infos[i].index;
            cse_variable_index varIndex = 2;
            rc = cse_observer_slave_get_real(observer, slaveIndex, &varIndex, 1, &value);
            if (rc < 0) {
                print_last_error();
                cse_execution_destroy(execution);
                return 1;
            }
            if (value != 0.05) {
                fprintf(stderr, "Expected value 0.05, got %f\n", value);
                cse_execution_destroy(execution);
                return 1;
            }
        }
    }

    cse_execution_start(execution);
    Sleep(100);
    cse_execution_stop(execution);

    return 0;
}
