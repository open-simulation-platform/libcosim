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

int main() {
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

    rc = cse_execution_step(execution, 3);
    if (rc < 0) {
        print_last_error();
        cse_execution_destroy(execution);
        return 1;
    }
    return 0;
}
