# libcosim (Python)

Python bindings for [libcosim](https://github.com/open-simulation-platform/libcosim),
generated with SWIG. Times are integer nanoseconds; use `libcosim.to_nanoseconds()`
and `libcosim.to_seconds()` to convert.

```python
import libcosim as cosim
loaded = cosim.load_execution_from_osp_config("OspSystemStructure.xml")
exe = loaded.execution
exe.simulate_until(cosim.to_nanoseconds(1.0))
```
