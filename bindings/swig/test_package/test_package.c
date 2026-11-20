/* Uses the installed libcosim-c package: version, error reporting, an
 * execution with a fixed-step algorithm. Plain C11. */
#include <cosim_c.h>

#include <stdio.h>

int main(void)
{
    cosim_set_output_level(cosim_level_warning);

    cosim_version* v = cosim_library_version();
    printf("libcosim %d.%d.%d\n",
        cosim_version_major_get(v), cosim_version_minor_get(v), cosim_version_patch_get(v));
    cosim_version_delete(v);

    /* A failing call returns NULL and leaves a pending error. */
    cosim_configured_execution* bad = cosim_load_execution_from_ssp("/nonexistent/system.ssp", NULL);
    if (bad != NULL || cosim_last_error_code() == 0) {
        fprintf(stderr, "expected an error\n");
        return 1;
    }
    const char* msg = cosim_last_error_message();
    printf("expected error %d: %s\n", cosim_last_error_code(), msg);
    cosim_free_string(msg);
    cosim_clear_last_error();

    cosim_fixed_step_algorithm* algo = cosim_fixed_step_algorithm_create(cosim_to_nanoseconds(0.1), 0);
    cosim_execution* exe = cosim_execution_new(0, (cosim_algorithm*)algo);
    if (cosim_execution_current_time(exe) != 0) return 1;
    cosim_execution_delete(exe);
    cosim_fixed_step_algorithm_delete(algo);

    printf("libcosim-c test_package OK\n");
    return 0;
}
