# libcosim for C

The C API (`libcosim-c`) is generated from libcosim's C++ API with SWIG's C
backend. It succeeds libcosimc, which is kept, deprecated, for existing code.

## Get it

From the OSP Conan remote:

```python
# conanfile.py
def requirements(self):
    self.requires("libcosim-c/<version>")
```

```cmake
find_package(libcosim-c REQUIRED)
target_link_libraries(myapp PRIVATE libcosim-c::cosim_c)
```

The header is `cosim_c.h`. The library is C, built on libcosim (C++), so
libcosim's runtime libraries must be found at run time (Conan's run
environment does that).

## The rules

The header starts with these rules; they apply to every function:

- **Ownership:** every object a function returns is owned by the caller and
  released with `cosim_<type>_delete()`, including elements returned by
  `cosim_<Container>_get()` and struct members returned by getters.
- **Strings:** every returned `const char*` is an owned copy, released with
  `cosim_free_string()` (not `free()`: on Windows the C runtimes may differ).
- **Errors:** a failing function returns 0 / `NULL` and leaves a pending
  error: `cosim_last_error_code()` (0: none, otherwise a `cosim_errc` value),
  `cosim_last_error_message()` (an owned string), `cosim_clear_last_error()`.
  The error stays pending until cleared.
- **Times** are `int64_t` nanoseconds; `cosim_to_nanoseconds(seconds)` and
  `cosim_to_seconds(ns)` convert.
- **Base classes:** pass derived objects where a base is expected with a cast,
  e.g. `(cosim_observer*)observer`, `(cosim_algorithm*)algorithm`.

## Example

```c
#include <cosim_c.h>
#include <stdio.h>
#include <string.h>

static int failed(const char* what)
{
    if (cosim_last_error_code() == 0) return 0;
    const char* msg = cosim_last_error_message();
    fprintf(stderr, "%s failed (%d): %s\n", what, cosim_last_error_code(), msg);
    cosim_free_string(msg);
    cosim_clear_last_error();
    return 1;
}

/* The value reference of a variable, by name. */
static unsigned int reference(cosim_execution* exe, int sim, const char* name)
{
    unsigned int ref = (unsigned int)-1;
    cosim_model_description* md = cosim_execution_get_model_description(exe, sim);
    cosim_VariableDescriptionVector* vars = cosim_model_description_get_variables(md);
    for (size_t i = 0; i < cosim_VariableDescriptionVector_size(vars); ++i) {
        cosim_variable_description* v = cosim_VariableDescriptionVector_get(vars, (int)i);
        const char* vname = cosim_variable_description_name_get(v);
        if (strcmp(vname, name) == 0) ref = cosim_variable_description_reference_get(v);
        cosim_free_string(vname);
        cosim_variable_description_delete(v);
    }
    cosim_VariableDescriptionVector_delete(vars);
    cosim_model_description_delete(md);
    return ref;
}

int main(void)
{
    cosim_set_output_level(cosim_level_warning);

    cosim_fixed_step_algorithm* algo = cosim_fixed_step_algorithm_create(cosim_to_nanoseconds(0.1), 0);
    cosim_execution* exe = cosim_execution_new(0, (cosim_algorithm*)algo);
    int sim = cosim_add_fmu(exe, "identity.fmu", "id", 0, NULL);
    if (failed("add_fmu")) return 1;

    cosim_last_value_observer* obs = cosim_last_value_observer_new();
    cosim_execution_add_observer(exe, (cosim_observer*)obs);
    cosim_execution_set_real_initial_value(exe, sim, reference(exe, sim, "realIn"), 2.5);

    cosim_execution_simulate_until(exe, cosim_to_nanoseconds(1.0));
    if (failed("simulate_until")) return 1;

    cosim_ValueReferenceVector* refs = cosim_ValueReferenceVector_new();
    cosim_ValueReferenceVector_push_back(refs, reference(exe, sim, "realOut"));
    cosim_DoubleVector* values = cosim_last_value_observer_get_real(obs, sim, refs);
    printf("t = %g s, realOut = %g\n",
        cosim_to_seconds(cosim_execution_current_time(exe)), cosim_DoubleVector_get(values, 0));

    cosim_DoubleVector_delete(values);
    cosim_ValueReferenceVector_delete(refs);
    cosim_last_value_observer_delete(obs);
    cosim_execution_delete(exe);
    cosim_fixed_step_algorithm_delete(algo);
    return 0;
}
```

## More

- Load a configuration: `cosim_load_execution_from_osp_config(path, options)`
  and `cosim_load_execution_from_ssp(path, options)` (`options` may be
  `NULL`) return a `cosim_configured_execution*`; get the execution with
  `cosim_configured_execution_get_execution()` and the simulators by name
  with `cosim_configured_execution_simulators()`.
- Overrides: `cosim_override_manipulator_new()`,
  `cosim_override_manipulator_override_real_variable(man, sim, ref, value)`
  (and `_integer_`, `_boolean_`, `_string_`), `..._reset_variable()`.
- Background run: `cosim_execution_start_simulation()`,
  `cosim_execution_get_status()`, `cosim_execution_stop_simulation()`.
- The complete API is the header; the functions follow libcosim's C++ API
  (`cosim_<class>_<method>`).

## Coming from libcosimc

libcosim-c is a different API: one function per C++ method, owned results,
a pending error instead of status codes. Some names are the same as in
libcosimc (`cosim_execution_step`, ...) with different signatures, so a
program uses one or the other. libcosimc remains available, deprecated,
until libcosim-c has proven itself.
