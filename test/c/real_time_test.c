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

void print_last_error() {
    fprintf(
        stderr,
        "Error code %d: %s\n",
        cse_last_error_code(), cse_last_error_message());
}

int main() {
    const char *dataDir = getenv("TEST_DATA_DIR");
    if (!dataDir) {
        fprintf(stderr, "Environment variable TEST_DATA_DIR not set\n");
        return 1;
    }

    char fmuPath1[1024];
    int rc = snprintf(fmuPath1, sizeof fmuPath1, "%s/fmi2/RoomHeating_OM_RH.fmu", dataDir);
    if (rc < 0) {
        perror(NULL);
        return 1;
    }
    char fmuPath2[1024];
    rc = snprintf(fmuPath2, sizeof fmuPath2, "%s/fmi2/WaterTank_Control.fmu", dataDir);
    if (rc < 0) {
        perror(NULL);
        return 1;
    }

    cse_execution *execution = cse_execution_create(0.0, 0.1);

    cse_slave *slave1 = cse_local_slave_create(fmuPath1);
    if (!slave1) {
        print_last_error();
        return 1;
    }
    cse_slave *slave2 = cse_local_slave_create(fmuPath2);
    if (!slave2) {
        print_last_error();
        return 1;
    }

    cse_execution_add_slave(execution, slave1);
    cse_execution_add_slave(execution, slave2);

    cse_execution_start(execution);

    Sleep(10000);

    cse_execution_stop(execution);

    cse_execution_status execution_status;
    rc = cse_execution_get_status(execution, &execution_status);
    if (rc < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }

    printf("Ran for what we think is 10 seconds, actual simulation time elapsed: %f\n", execution_status.current_time);


    rc = cse_execution_destroy(execution);
    if (rc < 0) {
        print_last_error();
        return 1;
    }

    return 0;
}
