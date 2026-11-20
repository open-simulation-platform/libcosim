# libcosim for Julia

`Libcosim` calls libcosim's C API (`libcosim-c`). Its functions have the C
names (`cosim_execution_step`, ...), with Julia conveniences:

- objects come back as handles that are released by the garbage collector;
- libcosim errors raise `Libcosim.CosimError` (fields `code`, `message`);
- strings come back as `String`s;
- times are `Int64` nanoseconds (`cosim_to_nanoseconds`, `cosim_to_seconds`).

## Install

```julia
import Pkg
Pkg.add("Libcosim")
```

Pkg downloads the libcosim binaries for your platform (Linux, Windows) as an
artifact. To use a libcosim-c build of your own instead, set
`LIBCOSIM_C_LIBRARY` to its path before loading the package.

## Example

```julia
using Libcosim
const L = Libcosim

L.cosim_set_output_level(L.cosim_level_warning)

# The value reference of a variable, by name
function reference(exe, sim, name)
    vars = L.cosim_model_description_get_variables(L.cosim_execution_get_model_description(exe, sim))
    for i in 0:Int(L.cosim_VariableDescriptionVector_size(vars))-1
        v = L.cosim_VariableDescriptionVector_get(vars, i)
        L.cosim_variable_description_name_get(v) == name && return L.cosim_variable_description_reference_get(v)
    end
    error("no variable $name")
end

algo = L.cosim_fixed_step_algorithm_create(L.cosim_to_nanoseconds(0.1), 0)
exe = L.cosim_execution_new(0, algo)
sim = L.cosim_add_fmu(exe, "identity.fmu", "id", 0, C_NULL)

obs = L.cosim_last_value_observer_new()
L.cosim_execution_add_observer(exe, obs)
L.cosim_execution_set_real_initial_value(exe, sim, reference(exe, sim, "realIn"), 2.5)

L.cosim_execution_simulate_until(exe, L.cosim_to_nanoseconds(1.0))

refs = L.cosim_ValueReferenceVector_new()
L.cosim_ValueReferenceVector_push_back(refs, reference(exe, sim, "realOut"))
values = L.cosim_last_value_observer_get_real(obs, sim, refs)
println(L.cosim_to_seconds(L.cosim_execution_current_time(exe)), " s: ",
        L.cosim_DoubleVector_get(values, 0))
```

Loading a configuration:

```julia
loaded = L.cosim_load_execution_from_osp_config("OspSystemStructure.xml", C_NULL)
exe = L.cosim_configured_execution_get_execution(loaded)
sim = L.cosim_StringIntMap_get(L.cosim_configured_execution_simulators(loaded), "TrueIdentity")
```

Errors:

```julia
try
    L.cosim_load_execution_from_ssp("missing.ssp", C_NULL)
catch e
    e isa L.CosimError && println(e.code == L.cosim_errc_zip_error, " ", e.message)
end
```

## Notes

- The functions are generated from the C header, one per C function; see
  [the C guide](c.md) for the API and `cosim_c.h` for the complete list.
  The C ownership rules are handled for you: do not call `*_delete` or
  `cosim_free_string`.
- Derived objects can be passed where the C API expects a base class
  (`obs` above is a `cosim_last_value_observer` passed as an observer).
