# FMI 3 core validation plan

Here we describe the validation plan for libcosim's core FMI3 support. We validate against data created with **FMPy**, an independent FMI implementation maintained by the Modelica Association for reference. For every validation case, FMPy and libcosim are run against the same FMU(s) with idential configuration (initial values, inputs and communication step); for all the new data types libcosim's samples must reproduce the validation samples from FMPy.

The scope are the new data types as they behave in import and co-simulation. Additional features are out of scope of this plan, and will be covered in separate pulls. 

## Components

| Component | Location |
|---|---|
| FMUs | Reference-FMUs at version`v0.0.42` built with `-DFMI_VERSION=3`. |
| Case definitions | `tests/data/fmi3/validation/cases/<case>.json` — FMU, step, stop, structural parameters, start values, stimuli, compared outputs. The single source read by both executors. |
| Generator | `tests/tools/gen_validation.py` — runs each case through FMPy (`pip install fmpy==0.3.32`) and writes `results/<case>.csv`. |
| libcosim harness | `tests/fmi3_validation_test.cpp`, one CTest per case, labelled with `fmi3_validation`. Parses the same json case definition, runs the case with libcosim, and applies the comparison rules between the libcosim data and the validation data.  |

### Comparison

The `time` values are considered to be valid  within error bounds of `+/- 1e-9`.  This is due to the fact that libcosim and FMPy model  time differently - libcosim uses `std::chrono`internally with an exact nanosecond count, and converts to `double`when a time value is requested; FMPy uses a Python `float64` directly.

Both implementations produce one row pr. step, starting at `t=start_time`. The rows then necessarily represent the same sample, so the comparison is aligned by row index, not time. The set of columns and the row count must naturally also match exactly. 

The column values must be an **exact** match for every integer type, boolean, enumeration, string (byte representation), binary, and bitwise comparison of `Float32`.  

The importers pass current `time` on to `fmi3DoStep(...)`. The reference FMUs all integrate from their internal `time`, so we can match exactly on values. If an FMUs integration depended on the input time, there would be a small difference in the `Float64` values and a tolerance check might need to be used. 

### Assumptions

Both importers and all FMUs must also satisfy these assumptions, or the results will not be an exact comparison. 

- Inputs are zero-order hold. This trivially holds for both importers.
- A value applied at `t_k` first appears in the sample at `t_k + h`.
- When the FMU requests termination from `DoStep`, the sample for that step is still recorded. FMPy reads it in Step Mode before `Terminate`; libcosim requires `Terminate` first and the harness reads it in the Terminated state. Both are spec-legal and the values are identical.
- Event Mode is **off**.
- Validation data is generated through FMPy's Python API directly, and not with the CLI. the CLI parses all inputs as `float64`, doesn't support `String` or `Binary`at all, and has some other limitations that hampers full validation testing. 

### Testing
The configuration for each case is kept in the corresponding  json in `tests/data/fmi3/validation/cases/<case>.json`. This is used by both libcosim and FMPy to run the tests. 

Running `tests/tools/gen_validation.py` will use FMPy to generate the validation data for each case below. 
`python tests/tools/gen_validation.py --fmu-dir build/tests/reference-fmus-v3-build/fmus`

Then compare with libcosim by running the corresponding C++ test `fmi3_validation_test.cpp`:
`ctest --test-dir build -C Release -L fmi3_validation --output-on-failure

A failing case leaves `results/<case>.libcosim.csv` next to the FMPy file for inspection.`

## Cases

Status: ✅ libcosim validated against FMPy · ❌ not yet runnable

| ID | Validates | FMU | Status |
|---|---|---|---|
| T1 | FMI 3 import; Float64 and Int32 outputs; parameter start values | `Dahlquist`, `BouncingBall`, `VanDerPol`, `Stair`, `Resource` | ✅ |
| T2 | Int8…UInt64 at their limits, without truncation, sign flip or widening | `Feedthrough` | ✅ |
| T3 | Float32 kept as `float` | `Feedthrough` | ✅ |
| T4 | Boolean; Enumeration as a distinct Int64-backed type | `Feedthrough` | ✅ |
| T5 | String and Binary byte-exact | `Feedthrough` | ✅ |
| T6 | Typed connections between two FMUs carry every type unchanged | `Feedthrough` × 2 | ❌ `execution` doesn't yet connect FMI3 types on this branch. |
| T7 | Arrays: row-major flat values, structural-sized shapes | `StateSpace` | ✅ |
| T8 | Structural parameters set in Configuration Mode reshape arrays | `StateSpace` | ✅ |

