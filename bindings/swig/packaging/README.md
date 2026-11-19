# Packaging the libcosim bindings

`bindings/swig` builds in two modes:

- **In-tree**, as part of the libcosim build (`-o swig_bindings=True`), for
  development and the binding tests (`ctest -R swig_`).
- **Standalone**, as its own CMake project against the **libcosim Conan
  package** (`find_package(libcosim)`). This is how the packages are made,
  and it means the bindings can move to their own repository (see below).

## Packages

| Language | Package | How | Native libraries |
|---|---|---|---|
| C | Conan package `libcosim-c` (`cosim_c` + `cosim_c.h`, CMake target `libcosim-c::cosim_c`) | `conan create .` (runs `test_package/`) | from Conan (libcosim and its dependencies are Conan requirements) |
| Python | wheel `libcosim` | scikit-build-core (`pyproject.toml`) | bundled in the wheel, `RPATH=$ORIGIN` |
| Java | `libcosim-<v>.jar` | CMake install component `java` | bundled in the JAR under `libcosim/native/<os>-<arch>/`, extracted to a temporary directory by `NativeLoader` on first use |
| C# | NuGet `Libcosim.<v>.nupkg` (net8.0) | CMake install component `csharp` + `dotnet pack` | `runtimes/<rid>/native/` (standard NuGet layout) |
| Julia | package directory `Libcosim/` | CMake install component `julia` | bundled in `Libcosim/lib/`; `LIBCOSIM_C_LIBRARY` overrides |

All five are built from the same SWIG interface (`cosim.i`). The Julia
package calls the C library; its `ccall` layer (`generated.jl`) is
generated from the C header by `julia/generate_julia.py`.

Each package carries libcosim and all its non-system dependencies (Boost,
Xerces-C, FMI Library, libzip, …). They are found with CMake's
`file(GET_RUNTIME_DEPENDENCIES)` and copied next to the binding library
(`cmake/BundleRuntime.cmake`); on Linux their RPATH is set to `$ORIGIN`
(needs `patchelf`). A package therefore needs nothing on `PATH` or
`LD_LIBRARY_PATH`. The size is dominated by these libraries (about 20 MB
unpacked on Linux; the wheel is 7.6 MB).

## Building

```sh
cd bindings/swig
python packaging/build_packages.py --test --conan-arg=-r=conancenter
conan create . --build=missing          # the C package, with test_package
```

`build_packages.py` runs `conan install` (libcosim, SWIG 4.4.1 and a JDK from
Conan), configures and builds the standalone project, installs each package
component, builds the wheel, and with `--test` tests every package with
nothing but the package itself on the search paths:

- Python: installs the wheel in a fresh venv, runs `python/tests/test_spike.py`
- Java: compiles and runs `java/test/libcosim/SmokeTest.java` against the JAR only
- C#: builds `csharp/consumer` from the `.nupkg` (private NuGet cache) and runs it
- Julia: runs `julia/test/runtests.jl` with `--project=dist/julia/Libcosim`

Output goes to `build-packages/dist/`. `--languages` selects a subset.
Needs Python with `build`, the .NET SDK and Julia for the respective
languages. The libcosim package is `libcosim/0.11.3@spike/testing` (export it
with `conan create` in the libcosim root, user/channel `spike/testing`), or
set `LIBCOSIM_REF`.

## Multi-platform packages

Each build produces packages for its own platform. For the release, a CI
matrix (Linux, Windows, macOS) would build them and

- Python: upload one wheel per platform and Python version (or use the
  stable ABI); cibuildwheel can drive this.
- Java: merge the `libcosim/native/<platform>/` trees into one JAR (the
  classes are identical); `NativeLoader` picks the right one at runtime.
- C#: copy the `runtimes/<rid>/` trees into one staging directory
  (`dist/csharp/stage`) and run `dotnet pack` once.
- Julia: either one package with `lib/<platform>/`, or (the Julia way) a
  JLL package built with BinaryBuilder.
- C: Conan builds per configuration anyway.

## Moving the bindings to their own repository

The standalone mode needs only the libcosim Conan package and the test data,
so `bindings/swig` can become its own repository (e.g. `libcosim-bindings`):

- **Recommended: depend on the libcosim Conan package** (as now, via
  `conanfile.py`). The bindings repository pins a libcosim version range;
  libcosim's own CI stays unaware of the bindings except for an optional
  downstream trigger. The binding version follows the libcosim version.
- **A git submodule of libcosim** would also work (in-tree mode, building
  libcosim from source), but couples the two histories, makes every binding
  build compile libcosim, and duplicates what the Conan package already
  provides. It is mainly useful if the bindings must be built against
  unreleased libcosim commits; a Conan package from a branch (as done in the
  spike, `@spike/testing`) covers that too.

What the separate repository needs from libcosim:

- the binding-friendly API (branch `feature/binding-friendly-api`) in a
  released libcosim;
- `find_dependency(libcbor)` in libcosim's CMake config (commit e60910e on
  `fix/libcosim-api-fixes`);
- test data: a few FMUs and OSP/SSP configurations, copied or fetched from
  libcosim's `tests/data` (the tests take the directory as
  `LIBCOSIM_SWIG_TEST_DATA_DIR`, `--test-data` or `LIBCOSIM_TEST_DATA`).
