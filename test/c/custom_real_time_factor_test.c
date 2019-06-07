#include <cse.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WINDOWS
#    include <windows.h>
#else

#    include <sys/time.h>
#    include <unistd.h>

#    define Sleep(x) usleep((x)*1000)

int64_t GetCurrentTime() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)((tv.tv_sec) * 1000 + (tv.tv_usec) / 1000);
}

#endif

void print_last_error() {
    fprintf(
            stderr,
            "Error code %d: %s\n",
            cse_last_error_code(), cse_last_error_message());
}

int main() {
    int exitCode = 0;

    cse_execution *execution = NULL;
    cse_slave *slave1 = NULL;
    cse_slave *slave2 = NULL;

    const char *dataDir = getenv("TEST_DATA_DIR");
    if (!dataDir) {
        fprintf(stderr, "Environment variable TEST_DATA_DIR not set\n");
        return 1;
    }

    char fmuPath1[1024];
    int rc = snprintf(fmuPath1, sizeof fmuPath1, "%s/fmi2/RoomHeating_OM_RH.fmu", dataDir);
    if (rc < 0) {
        perror(NULL);
        goto Lfailure;
    }

    double stepSize = 0.1;
    double real_time_factor = 2.25;
    int64_t nanoStepSize = (int64_t)(stepSize * 1.0e9);

    execution = cse_execution_create(0, nanoStepSize);
    if (!execution) { goto Lerror; }

    slave1 = cse_local_slave_create(fmuPath1);
    if (!slave1) { goto Lerror; }

    int idx1 = cse_execution_add_slave(execution, slave1);
    if (idx1 < 0) { goto Lerror; }

    rc = cse_execution_enable_real_time_simulation(execution);
    if (rc < 0) { goto Lerror; }

    cse_execution_status executionStatus;
    rc = cse_execution_get_status(execution, &executionStatus);
    if (rc < 0) { goto Lerror; }

    rc = cse_execution_start(execution);
    if (rc < 0) { goto Lerror; }

    Sleep(1000);

    rc = cse_execution_set_custom_real_time_factor(execution, real_time_factor);

    Sleep(4000);
    rc = cse_execution_stop(execution);
    if (rc < 0) { goto Lerror; }

    rc = cse_execution_get_status(execution, &executionStatus);
    if (rc < 0) { goto Lerror; }

    double rtf = executionStatus.real_time_factor;
    printf("The setpoint real time factor is %f\n", real_time_factor);
    printf("The measured real time factor is %f\n", rtf);
    if (fabs(rtf - real_time_factor) > 1.0e-2) {
        fprintf(stderr, "Expected real time factor == %f, got %f\n", real_time_factor, rtf);
        goto Lfailure;
    }

    goto Lcleanup;

    Lerror:
    print_last_error();

    Lfailure:
    exitCode = 1;

    Lcleanup:
    cse_local_slave_destroy(slave2);
    cse_execution_destroy(execution);

    return exitCode;
}