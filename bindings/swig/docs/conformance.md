# Binding conformance

What every libcosim binding must do, independent of the language, and which
tests check it. A binding conforms when every applicable item is covered by
a passing test. New features of the bindings get an item here and a test in
each binding; the matrix below shows the gaps.

The items describe behaviour, not syntax: "raises an error" is an exception
in Python, Java, C# and Julia, and a pending error (`cosim_last_error_code()`)
in C.

## Items

**Library and errors**

- **CF-1 Version.** The libcosim version is available.
- **CF-2 Errors carry the code.** A failing call (e.g. loading a missing SSP
  file) raises an error with libcosim's error code (`cosim::errc`, here
  `zip_error`) and a message. No partial result is returned.
- **CF-3 Errors are recoverable.** After an error, the binding keeps working;
  in C, the pending error can be read and cleared.
- **CF-4 Invalid access is an error, not a crash** (e.g. an out-of-range
  index into a returned vector).
- **CF-5 Logging.** The output level can be set.

**Loading**

- **CF-10 OSP configuration.** An `OspSystemStructure.xml` loads; its
  simulators are available by name.
- **CF-11 SSP.** An SSP package or directory loads.
- **CF-12 Load options.** Start time, algorithm and worker thread count can be
  given when loading.
- **CF-13 End time.** A configured end time is available.
- **CF-14 Paths.** Configuration and FMU paths are accepted as strings, and
  as the language's path type where it has one (Python `os.PathLike`).

**Building an execution in code**

- **CF-20 Execution with an algorithm.** An execution can be created with a
  fixed-step algorithm and a start time.
- **CF-21 Adding FMUs.** FMUs can be added by path, with a name; the
  simulators can be listed (name and index).
- **CF-22 ECCO.** An ECCO algorithm can be configured (step sizes in
  nanoseconds) and used, including power bonds and their energies.
- **CF-23 Initial values.** Initial values of all four types can be set.

**Time**

- **CF-30 Nanoseconds.** All times and durations are 64-bit integer
  nanoseconds; `to_nanoseconds` and `to_seconds` convert.
- **CF-31 No silent float time.** Where the language would otherwise accept
  a floating-point time silently, it is rejected (Python).

**Variables**

- **CF-40 Model description.** A simulator's model description lists its
  variables with name, reference, type, causality and variability.
- **CF-41 Shared enumerator names.** Enumerators have the same names in all
  bindings (`boolean_value`, `string_value`, `fixed_value` for the ones that
  are keywords somewhere); no binding-specific renames.

**Running**

- **CF-50 Stepping.** `step` advances by one step and returns its size;
  `simulate_until` runs to a time.
- **CF-51 Asynchronous run.** `start_simulation` runs in the background;
  `get_status` reports state (running, stopped, error), time and error;
  `stop_simulation` stops it; starting a running execution is an error.
- **CF-52 Real-time.** Real-time mode and the real-time factor target can be
  set and read; an invalid factor is an error.
- **CF-53 State export and import.** The execution state can be exported as
  bytes (the language's byte type) after initialization and imported again;
  the execution continues from the imported time with the same results.
  Exporting before initialization, and importing invalid data, are errors.

**Observing and manipulating**

- **CF-60 Last values.** A last-value observer returns the latest values of
  all four types.
- **CF-61 Overrides.** An override manipulator overrides variables of all
  four types, and an override can be reset.
- **CF-62 Time series.** A time-series observer returns samples (values,
  steps, times) of observed real and integer variables.
- **CF-63 File observer.** A file observer records to a directory.

**Memory**

- **CF-70 Ownership.** Every object the API returns is owned by the caller
  and released by the binding's normal mechanism (garbage collection,
  `*_delete` in C); no use-after-free when a returned object outlives the
  object it came from (e.g. the variables of a temporary model
  description).
- **CF-71 Strings.** Strings are returned as the language's strings (in C as
  owned copies, released with `cosim_free_string`).

## Coverage

✅ tested · 🟡 partly tested · — not tested · n/a not applicable

| Item | Python | C | Julia | Java | C# |
|---|---|---|---|---|---|
| CF-1 Version | ✅ | ✅ | ✅ | — | — |
| CF-2 Errors carry the code | ✅ | ✅ | ✅ | ✅ | ✅ |
| CF-3 Errors are recoverable | ✅ | ✅ | ✅ | ✅ | ✅ |
| CF-4 Invalid access is an error | — | ✅ | — | — | — |
| CF-5 Logging | ✅ | ✅ | ✅ | ✅ | ✅ |
| CF-10 OSP configuration | ✅ | ✅ | ✅ | ✅ | ✅ |
| CF-11 SSP | ✅ | ✅ | ✅ | — | — |
| CF-12 Load options | ✅ | 🟡 | 🟡 | — | — |
| CF-13 End time | ✅ | ✅ | ✅ | — | — |
| CF-14 Paths | ✅ | ✅ | ✅ | ✅ | ✅ |
| CF-20 Execution with an algorithm | ✅ | ✅ | ✅ | ✅ | ✅ |
| CF-21 Adding FMUs | ✅ | ✅ | ✅ | ✅ | ✅ |
| CF-22 ECCO | ✅ | — | — | 🟡 | 🟡 |
| CF-23 Initial values | ✅ | ✅ | ✅ | — | — |
| CF-30 Nanoseconds | ✅ | ✅ | ✅ | ✅ | ✅ |
| CF-31 No silent float time | ✅ | n/a | n/a | n/a | n/a |
| CF-40 Model description | ✅ | ✅ | ✅ | ✅ | ✅ |
| CF-41 Shared enumerator names | ✅ build check | ✅ build check | ✅ build check | ✅ build check | ✅ build check |
| CF-50 Stepping | ✅ | ✅ | ✅ | ✅ | ✅ |
| CF-51 Asynchronous run | ✅ | ✅ | ✅ | ✅ | ✅ |
| CF-52 Real-time | ✅ | ✅ | ✅ | — | — |
| CF-53 State export and import | ✅ | ✅ valgrind | ✅ | ✅ | ✅ |
| CF-60 Last values | ✅ | ✅ | ✅ | 🟡 | 🟡 |
| CF-61 Overrides | ✅ | ✅ | 🟡 | 🟡 | 🟡 |
| CF-62 Time series | ✅ | ✅ | ✅ | — | — |
| CF-63 File observer | ✅ | — | — | — | — |
| CF-70 Ownership | ✅ | ✅ valgrind | ✅ finalizers | 🟡 | 🟡 |
| CF-71 Strings | ✅ | ✅ | ✅ | ✅ | ✅ |

🟡 means partly tested: load options with the start time only (C, Julia);
Julia overrides without booleans; Java and C# values and overrides for
integers only, ECCO configured and stepped without power bonds, and memory
left to the garbage collector without a targeted test.

Remaining gaps in the priority bindings (Python, C, Julia): invalid access
(CF-4) in Julia, ECCO (CF-22) and the file observer (CF-63) in C and Julia.

CF-53 needs an FMU that supports state export (`LIBCOSIM_SWIG_STATE_FMU`;
in libcosim's build with tests, the Dahlquist reference FMU); without it the
state tests report themselves as skipped.

Tests: `python/tests/test_bindings.py`, `c/smoke_test.c` (with the C API
drift check `swig_c_api_reference`), `julia/test/runtests.jl`,
`java/test/libcosim/SmokeTest.java`, `csharp/test/SmokeTest.cs`; CF-41 is the
`swig_keyword_check` build step. The same functions are generated for every
language from one interface, so a gap in a test is less likely to be a gap
in the binding than with hand-written bindings. Java and C# are best effort;
their tests cover the core items.
