#include <cse.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    slave = cse_local_slave_create(fmuPath);
    if (!slave) { goto Lerror; }

    cse_slave_index slaveIndex = cse_execution_add_slave(execution, slave);
    if (slaveIndex < 0) { goto Lerror; }

    cse_model_info info;

    cse_get_model_info(execution, slaveIndex, &info);

    const char* expected_name = "no.viproma.demo.identity";
    if (0 != strncmp(info.name, expected_name, SLAVE_NAME_MAX_SIZE)) {
        fprintf(stderr, "Expected name to be %s, got: %s\n", expected_name, info.name);
        goto Lfailure;
    }

    const char* expected_uuid = "ae713a03-634c-5da4-802e-9ea653e11f42";
    if (0 != strncmp(info.uuid, expected_uuid, SLAVE_NAME_MAX_SIZE)) {
        fprintf(stderr, "Expected uuid to be %s, got: %s\n", expected_uuid, info.uuid);
        goto Lfailure;
    }

    const char* expected_version = "0.3";
    if (0 != strncmp(info.version, expected_version, SLAVE_NAME_MAX_SIZE)) {
        fprintf(stderr, "Expected version to be %s, got: %s\n", expected_version, info.version);
        goto Lfailure;
    }

    const char* expected_author = "Lars Tandle Kyllingstad";
    if (0 != strncmp(info.author, expected_author, SLAVE_NAME_MAX_SIZE)) {
        fprintf(stderr, "Expected author to be %s, got: %s\n", expected_author, info.author);
        goto Lfailure;
    }

    const char* expected_description = "Has one input and one output of each type, and outputs are always set equal to inputs";
    if (0 != strncmp(info.description, expected_description, SLAVE_NAME_MAX_SIZE)) {
        fprintf(stderr, "Expected description to be %s, got: %s\n", expected_description, info.description);
        goto Lfailure;
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
