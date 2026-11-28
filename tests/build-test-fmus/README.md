# Building the test FMUs

This directory contains the recipes, fixture sources and patches used to
generate the test FMUs. The test-specific staging and registrations remain
in `tests/cmake` and `tests/CMakeLists.txt`.

In a libcosim build configured with `LIBCOSIM_BUILD_TESTS=ON`, build all
fixtures and the native-platform archive explicitly:

```shell
cmake --build <build-directory> --config Release --target build-test-fmus --parallel
```

The output is `test-fmu-generator/generated-test-fmus.zip` beneath the
build directory. Its `fmi1`, `fmi2` and `fmi3` directories group archives by
FMI version; the two special `Stair.fmu` variants have separate subdirectories.
The ordinary test build does not run this generator or build FMUs. Tests use
prebuilt FMUs from `tests/data`. Unpack the generated archive into
`tests/data` before running tests, preserving its versioned directories. In
particular, `fmi3/StepProbe.fmu` supplies the modified BouncingBall used for
early-return and event-timing tests; `fmi3/StatusFault.fmu` supplies status
injection and instance cleanup after Fatal. If an archive is missing, generate
or obtain it separately rather than relying on the test build.

The directory can also be configured as a standalone CMake project. Provide a
Reference-FMUs `v0.0.42` source tree, the FMI Library `3.0.4` standard headers
under `ThirdParty/FMI/default`, a C/C++ toolchain, Python 3 and GNU patch:

```shell
cmake -S tests/build-test-fmus -B <fixture-build-directory> \
  -DREFERENCE_FMUS_SOURCE_DIR=<reference-fmus-source> \
  -DFMILibrary_INCLUDE_DIRS=<fmi-library-source>/ThirdParty/FMI/default
cmake --build <fixture-build-directory> --config Release --target build-test-fmus --parallel
```

On Windows, use a short fixture build path to avoid MSBuild path-length limits.

The manually dispatched **Build test FMU fixtures** GitHub Actions workflow
builds Windows, Linux and both macOS architectures, merges their binaries
into each FMU, and uploads a single version-grouped ZIP. Those merged FMUs
also contain the rebuild sources, provenance and relevant patches.
