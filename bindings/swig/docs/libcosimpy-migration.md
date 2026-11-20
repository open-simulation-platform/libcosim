# Migrating from libcosimpy

libcosimpy is a ctypes wrapper of libcosimc. The `libcosim` Python package
binds libcosim directly. There are two ways to move:

1. **Keep using libcosimpy.** Its next version runs on the `libcosim`
   package instead of libcosimc (pure Python, same API, its own test suite
   passes unchanged). Upgrading libcosimpy is all it takes; nothing in your
   code changes. This is the route for existing code.
2. **Use `libcosim` directly**, for new code or when you touch code anyway.
   The rest of this guide maps libcosimpy to `libcosim`.

## What changes

| | libcosimpy | libcosim |
|---|---|---|
| Import | `from libcosimpy.CosimExecution import CosimExecution`, one module per class | `import libcosim as cosim` |
| Times | `int` nanoseconds | `int` nanoseconds (the same); `to_nanoseconds()` / `to_seconds()` |
| ECCO parameters | durations in seconds | durations in nanoseconds |
| Names | `bytes` (`info.name.decode()`) | `str` |
| Errors | methods return `False`; some raise `AssertionError` | `CosimError` (with message and libcosim's error code) |
| Enumerations | `CosimVariableType.REAL` (Python `Enum`) | `cosim.variable_type_real` (ints, same names in every language) |
| Simulators and variables | indices and value references | the same, or by name: `exe.variable("Crane", "angle")` |

## Mapping

**Creating an execution**

| libcosimpy | libcosim |
|---|---|
| `CosimExecution.from_step_size(step)` | `cosim.execution(0, cosim.fixed_step_algorithm(step))` |
| `CosimExecution.from_algorithm(algo)` | `cosim.execution(0, algo)` |
| `CosimExecution.from_osp_config_file(path)` | `cosim.load_execution_from_osp_config(path).execution` |
| `CosimExecution.from_ssp_file(path, step_size=s)` | `options = cosim.execution_load_options(); options.algorithm = cosim.fixed_step_algorithm(s)`; `cosim.load_execution_from_ssp(path, options).execution` |
| `execution.add_local_slave(CosimLocalSlave(fmu, name))` | `cosim.add_fmu(exe, fmu, name)` (returns the index) |
| `CosimAlgorithm.create_ecco_algorithm(EccoParams(...))` | `params = cosim.ecco_algorithm_params()` (fields in nanoseconds); `cosim.ecco_algorithm(params)` |
| `algorithm.add_power_bond(...)` | `algorithm.add_power_bond(input_a, output_a, input_b, output_b)` with `variable_id`s |

**Running**

| libcosimpy | libcosim |
|---|---|
| `execution.step(n)` | `exe.step()` (one step; returns its size) |
| `execution.simulate_until(t)` | `exe.simulate_until(t)` |
| `execution.start()` / `stop()` | `exe.start_simulation()` / `stop_simulation()`, or `with exe.running():` |
| `execution.status()` | `exe.get_status()` (and `exe.current_time()`) |
| `real_time_simulation_enabled(b)`, `real_time_factor_target(f)` | `exe.set_real_time_simulation(b)`, `exe.set_real_time_factor_target(f)` |

**Simulators and variables**

| libcosimpy | libcosim |
|---|---|
| `execution.slave_infos()` | `exe.get_simulators()` (`name`, `index`) |
| `execution.slave_index_from_instance_name(n)` | `exe.simulator_index(n)` |
| `execution.slave_variables(i)` | `exe.get_model_description(i).variables`, or `exe.variables(i)` (by name) |
| `execution.real_initial_value(i, ref, v)` | `exe.set_real_initial_value(i, ref, v)`, or `exe.set_initial_value(var, v)` |
| `execution.connect_real_variables(o, oref, i, iref)` | `exe.connect_variables(output_var, input_var)` (`variable_id`s) |

**Observers and manipulators**

| libcosimpy | libcosim |
|---|---|
| `CosimObserver.create_last_value()` | `cosim.last_value_observer()` |
| `observer.last_real_values(i, [refs])` | `observer.get_real(i, [refs])`, or `observer.value(var)` |
| `CosimObserver.create_time_series()`, `start_time_series(...)` | `cosim.time_series_observer()`, `start_observing(var)` |
| `observer.time_series_real_samples(i, ref, from_step, n)` | `observer.get_real_samples(i, ref, from_step, n)`, or `samples(var, from_step, n)` |
| `CosimObserver.create_to_dir(dir)` / `from_cfg(cfg, dir)` | `cosim.file_observer(dir)` / `cosim.file_observer(dir, cfg)` |
| `CosimManipulator.create_override()` | `cosim.override_manipulator()` |
| `manipulator.slave_real_values(i, [refs], [values])` | `manipulator.override_real_variable(i, ref, value)`, or `override(var, value)` |
| `manipulator.reset_variables(i, type, [refs])` | `manipulator.reset_variable(i, type, ref)`, or `reset(var)` |
| `CosimManipulator.create_scenario_manager()`, `execution.load_scenario(m, file)` | `cosim.scenario_manager()`, `m.load_scenario(file, exe.current_time())` |
| `execution.add_observer(o)` / `add_manipulator(m)` | `exe.add_observer(o)` / `exe.add_manipulator(m)` |
| `log_output_level(CosimLogLevel.WARNING)` | `cosim.set_output_level(cosim.level_warning)` |

## Example

libcosimpy:

```python
from libcosimpy.CosimExecution import CosimExecution
from libcosimpy.CosimObserver import CosimObserver
from libcosimpy.CosimManipulator import CosimManipulator

execution = CosimExecution.from_osp_config_file("OspSystemStructure.xml")
sim = execution.slave_index_from_instance_name("TrueIdentity")
refs = {v.name.decode(): v.reference for v in execution.slave_variables(sim)}

observer = CosimObserver.create_last_value()
manipulator = CosimManipulator.create_override()
execution.add_observer(observer)
execution.add_manipulator(manipulator)

manipulator.slave_real_values(sim, [refs["realIn"]], [1.5])
execution.simulate_until(1_000_000_000)
print(observer.last_real_values(sim, [refs["realOut"]]))
```

libcosim:

```python
import libcosim as cosim

exe = cosim.load_execution_from_osp_config("OspSystemStructure.xml").execution
real_in = exe.variable("TrueIdentity", "realIn")
real_out = exe.variable("TrueIdentity", "realOut")

observer = cosim.last_value_observer()
manipulator = cosim.override_manipulator()
exe.add_observer(observer)
exe.add_manipulator(manipulator)

manipulator.override(real_in, 1.5)
exe.simulate_until(cosim.to_nanoseconds(1.0))
print(observer.value(real_out))
```

See the [Python guide](python.md) for more.
