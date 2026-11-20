# libcosim for Python

## Install

```sh
pip install libcosim
```

The wheel contains libcosim and its dependencies; it works with CPython 3.10
and later on Linux and Windows.

## Run a system

```python
import libcosim as cosim

cosim.set_output_level(cosim.level_warning)

# Load an OSP system structure (or an SSP: load_execution_from_ssp)
loaded = cosim.load_execution_from_osp_config("OspSystemStructure.xml")
exe = loaded.execution
print(dict(loaded.simulators()))          # {name: simulator index}

exe.simulate_until(cosim.to_nanoseconds(1.0))
print(cosim.to_seconds(exe.current_time()))
```

**Times are integer nanoseconds** everywhere (`current_time()`, `step()`,
`simulate_until()`, step sizes). Convert with `to_nanoseconds(seconds)` and
`to_seconds(nanoseconds)`; passing a float where a time is expected raises a
`TypeError`.

## Build a system in code, read and set values

```python
import libcosim as cosim

exe = cosim.execution(0, cosim.fixed_step_algorithm(cosim.to_nanoseconds(0.1)))
cosim.add_fmu(exe, "identity.fmu", "id")      # returns the simulator index

real_in = exe.variable("id", "realIn")        # a variable_id, by names
real_out = exe.variable("id", "realOut")
print(exe.variables("id").keys())             # all variables of a simulator

observer = cosim.last_value_observer()
manipulator = cosim.override_manipulator()
exe.add_observer(observer)
exe.add_manipulator(manipulator)

exe.set_initial_value(real_in, 2.5)
exe.step()
exe.step()
print(observer.value(real_out))               # 2.5

manipulator.override(real_in, 1.5)            # until reset(real_in)
exe.simulate_until(cosim.to_nanoseconds(1.0))
print(observer.value(real_out))               # 1.5
```

`value`, `override` and `set_initial_value` work for real, integer, boolean
and string variables. The type-specific functions of the underlying API are
available too (`observer.get_real(simulator, [references])`,
`manipulator.override_real_variable(...)`, `exe.set_real_initial_value(...)`).

## Time series

```python
series = cosim.time_series_observer()
exe.add_observer(series)
series.start_observing(real_out)
exe.simulate_until(cosim.to_nanoseconds(2.0))

samples = series.samples(real_out, 1, 100)    # from step 1, at most 100
print(list(samples.values), list(samples.get_times_seconds()))
```

For results on disk, add a `cosim.file_observer(directory)`.

## Run in the background

```python
import time

with exe.running():                           # start_simulation() ... stop_simulation()
    time.sleep(0.5)
    print(exe.get_status())                   # execution_status(state=running, ...)
```

The simulation runs in a background thread; `with exe.running()` stops it
and waits for it at the end of the block. For real time,
`exe.set_real_time_simulation(True)` and `exe.set_real_time_factor_target(1.0)`.

## Checkpoints: saving and restoring the state

```python
exe.step()                                    # the execution must be initialized
state = exe.export_current_state_bytes()      # bytes
...
exe.import_state_bytes(state)                 # back to the saved time and state
```

The bytes can be kept in memory or written to a file. Every FMU must support
state export (`exe.get_model_description(i).capabilities.can_export_state`).
The state does not include the system structure: import it into an execution
set up the same way. Not possible while a simulation runs in the background.

## Errors

libcosim errors raise `cosim.CosimError` (a `RuntimeError`), with the
message and libcosim's error code:

```python
try:
    cosim.load_execution_from_ssp("missing.ssp")
except cosim.CosimError as e:
    message, code = e.args
    print(code == cosim.errc_zip_error, message)
```

## Notes

- The API follows libcosim's C++ API; names are the same in all languages
  (enumerators that are keywords somewhere end in `_value`:
  `variable_type_boolean_value`, `variable_type_string_value`,
  `variable_variability_fixed_value`).
- Objects returned by the API are independent copies or shared references;
  they stay valid as long as you hold them.
- Coming from libcosimpy: see [migrating from libcosimpy](libcosimpy-migration.md).
