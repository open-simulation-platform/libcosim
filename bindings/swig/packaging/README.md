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
| Python | wheel `libcosim`, one per platform for CPython ≥ 3.10 (stable ABI, `abi3`) | scikit-build-core (`pyproject.toml`) | bundled in the wheel, `RPATH=$ORIGIN` |
| Java | `libcosim-<v>.jar` | CMake install component `java` | bundled in the JAR under `libcosim/native/<os>-<arch>/`, extracted to a temporary directory by `NativeLoader` on first use |
| C# | NuGet `Libcosim.<v>.nupkg` (net8.0) | CMake install component `csharp` + `dotnet pack` | `runtimes/<rid>/native/` (standard NuGet layout) |
| Julia | package `Libcosim` | CMake install component `julia` + `julia/make_artifact.jl` | the `libcosim_c` artifact (tarball per platform, listed in `Artifacts.toml`, downloaded by Pkg on install); `LIBCOSIM_C_LIBRARY` overrides |

All five are built from the same SWIG interface (`cosim.i`). The Julia
package calls the C library; its `ccall` layer (`generated.jl`) is
generated from the C header by `julia/generate_julia.py`.

Each package carries libcosim and all its non-system dependencies (Boost,
Xerces-C, FMI Library, libzip, …). They are found with CMake's
`file(GET_RUNTIME_DEPENDENCIES)` and copied next to the binding library
(`cmake/BundleRuntime.cmake`); on Linux their RPATH is set to `$ORIGIN`
(needs `patchelf`). A package therefore needs nothing on `PATH` or
`LD_LIBRARY_PATH`. The size is dominated by these libraries (about 20 MB
unpacked on Linux; the wheel, JAR and NuGet package are about 7 MB on Linux
and 3.4 MB on Windows).

### Proxy FMUs

The packages support proxy FMUs (FMUs run in separate processes, e.g. for
isolation or other bitness): libcosim is built with proxyfmu, and every
package contains the `proxyfmu` executable (and its libraries) next to the
binding's libraries. The proxyfmu client finds it there, whatever program
loads the bindings. This needs a proxyfmu release whose client searches next
to its client library (proxy-fmu PR #110, stacked on #108 and #109), and
libcosim's recipe requiring it; with proxyfmu 0.4.3 the executable is not
found. `-o "&:proxyfmu=False"` (via `--conan-arg`) builds packages without
it. The package tests run a proxy-FMU configuration when the package
contains the executable.

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

- Python: installs the wheel in a fresh venv, runs `python/tests/test_bindings.py`
- Java: compiles and runs `java/test/libcosim/SmokeTest.java` against the JAR only
- C#: builds `csharp/consumer` from the `.nupkg` (private NuGet cache) and runs it
- Julia: installs the package with `Pkg.develop` in a fresh depot, Pkg
  downloading the artifact from the local tarball (`file://` URL), and runs
  `julia/test/runtests.jl`

Output goes to `build-packages/dist/`. `--languages` selects a subset.
Needs Python with `build`, the .NET SDK and Julia for the respective
languages.

The bindings build against the released libcosim of the same version,
`libcosim/<version>@osp/stable` from the OSP remote. Before that release
exists, create the package from the libcosim checkout and point
`LIBCOSIM_REF` at it:

```sh
conan create <libcosim> --user local --channel testing --build=missing
export LIBCOSIM_REF=libcosim/<version>@local/testing
```

## Multi-platform packages and distribution

Each build produces packages for its own platform. The release workflow
(`.github/workflows/release-bindings.yml`) runs when libcosim's Conan
workflow has published `libcosim/<v>@osp/stable` for a `v*` tag; it builds
all platforms against that package and publishes:

| Binding | Built by | Published to |
|---|---|---|
| Python | cibuildwheel (`packaging/cibw_before_all.py` provides libcosim and SWIG through Conan; manylinux on Linux) | PyPI, OSP organization (trusted publishing) |
| C | `conan create .` per platform | OSP Conan remote |
| Julia | `build_packages.py` per platform; `merge_packages.py` writes one `Artifacts.toml` | artifact tarballs on the GitHub release; `Libcosim.jl.zip` (package source) to commit to the Julia package repository and register in General |
| Java | `build_packages.py` per platform; `merge_packages.py` merges the `libcosim/native/<platform>/` trees into one JAR | GitHub release |
| C# | `build_packages.py` per platform; `merge_packages.py` merges the `runtimes/<rid>/` trees into one `.nupkg` | GitHub release (use as a local NuGet source) |

The Julia artifact URLs point to the GitHub release of the version
(`--julia-url` overrides). macOS joins the matrices when libcosim supports it.

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
  unreleased libcosim commits; a Conan package from a branch
  (`@local/testing`, or libcosim's own `@osp/testing-<branch>` builds)
  covers that too.

What the separate repository needs from libcosim:

- the binding-friendly API (branch `feature/binding-friendly-api`) in a
  released libcosim;
- `find_dependency(libcbor)` in libcosim's CMake config (commit e60910e on
  `fix/libcosim-api-fixes`);
- test data: a few FMUs and OSP/SSP configurations, copied or fetched from
  libcosim's `tests/data` (the tests take the directory as
  `LIBCOSIM_SWIG_TEST_DATA_DIR`, `--test-data` or `LIBCOSIM_TEST_DATA`).
