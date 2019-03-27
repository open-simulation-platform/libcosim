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

int64_t GetCurrentTime()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)((tv.tv_sec) * 1000 + (tv.tv_usec) / 1000);
}
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
    cse_slave* slave1 = NULL;
    cse_slave* slave2 = NULL;

    const char* dataDir = getenv("TEST_DATA_DIR");
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
    char fmuPath2[1024];
    rc = snprintf(fmuPath2, sizeof fmuPath2, "%s/fmi2/WaterTank_Control.fmu", dataDir);
    if (rc < 0) {
        perror(NULL);
        goto Lfailure;
    }

    double stepSize = 0.1;
    int64_t nanoStepSize = (int64_t)(stepSize * 1.0e9);

    execution = cse_execution_create(0, nanoStepSize);
    if (!execution) { goto Lerror; }

    slave1 = cse_local_slave_create(fmuPath1);
    if (!slave1) { goto Lerror; }

    slave2 = cse_local_slave_create(fmuPath2);
    if (!slave2) { goto Lerror; }

    int idx1 = cse_execution_add_slave(execution, slave1);
    if (idx1 < 0) { goto Lerror; }
    int idx2 = cse_execution_add_slave(execution, slave2);
    if (idx2 < 0) { goto Lerror; }

    cse_execution_status executionStatus;
    rc = cse_execution_get_status(execution, &executionStatus);
    if (rc < 0) { goto Lerror; }
    if (!executionStatus.is_real_time_simulation) {
        cse_execution_enable_real_time_simulation(execution);
    }

    rc = cse_execution_start(execution);
    if (rc < 0) { goto Lerror; }
    int64_t before1 = GetCurrentTime();
    Sleep(2000);
    rc = cse_execution_stop(execution);
    if (rc < 0) { goto Lerror; }
    int64_t after1 = GetCurrentTime();

    Sleep(1000);

    rc = cse_execution_start(execution);
    if (rc < 0) { goto Lerror; }
    int64_t before2 = GetCurrentTime();
    Sleep(2000);
    rc = cse_execution_stop(execution);
    if (rc < 0) { goto Lerror; }
    int64_t after2 = GetCurrentTime();

    int64_t actualElapsed = (after1 - before1) + (after2 - before2);
    double elapsedS = actualElapsed * 1.0e-3;

    int rounded = (int)(0.5 + elapsedS / stepSize);
    double elapsed = rounded * stepSize;

    rc = cse_execution_get_status(execution, &executionStatus);
    if (rc < 0) { goto Lerror; }

    double simTime = executionStatus.current_time * 1e-9;
    if (fabs(simTime - elapsed) > 1.0e-3) {
        fprintf(stderr, "Expected final simulation time == %f, got %f\n", elapsedS, simTime);
        goto Lfailure;
    }

    goto Lcleanup;

Lerror:
    print_last_error();

Lfailure:
    exitCode = 1;

Lcleanup:
    cse_local_slave_destroy(slave2);
    cse_local_slave_destroy(slave1);
    cse_execution_destroy(execution);

    return exitCode;
}
