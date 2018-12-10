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
    printf("Num slaves: %zu\n", numSlaves);

    cse_slave_info infos[2];
    rc = cse_execution_get_slave_infos(execution, &infos[0], numSlaves);
    if (rc < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    for (size_t i = 0; i < numSlaves; i++) {
        printf("Name: %s\n", infos[i].name);
        printf("Source: %s\n", infos[i].source);
        printf("Index: %d\n", infos[i].index);
    }

    double values = -1;
    cse_slave_index slaveIndex = 0; // what is it?
    cse_variable_index varIndex = 0; // what is it?
    cse_observer_slave_get_real(observer, slaveIndex, &varIndex, 1, &values);

    return 0;
}
