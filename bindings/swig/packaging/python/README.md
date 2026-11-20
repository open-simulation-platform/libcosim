# libcosim (Python)

Python bindings for [libcosim](https://github.com/open-simulation-platform/libcosim),
the co-simulation library of the Open Simulation Platform (FMI, OSP and SSP
system structures). Times are integer nanoseconds; use
`libcosim.to_nanoseconds()` and `libcosim.to_seconds()` to convert.

```python
import libcosim as cosim

exe = cosim.load_execution_from_osp_config("OspSystemStructure.xml").execution

observer = cosim.last_value_observer()
exe.add_observer(observer)
angle = exe.variable("Crane", "angle")      # by simulator and variable name

exe.simulate_until(cosim.to_nanoseconds(1.0))
print(observer.value(angle))
```

The package includes libcosim and its dependencies. The API follows
libcosim's C++ API, with Pythonic additions (names instead of indices,
type-independent `value`/`override`/`set_initial_value`, `with exe.running():`).
