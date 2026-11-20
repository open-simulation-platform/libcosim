# libcosim language bindings

Bindings of libcosim for **Python, C, Julia, Java and C#**, generated with
[SWIG](https://swig.org) from one shared interface (`cosim.i`). Julia uses the
C library through `ccall`, with a layer generated from the C header.

| Language | Package | Distribution |
|---|---|---|
| Python | `libcosim` wheel | PyPI |
| C | Conan package `libcosim-c` (`cosim_c.h`) | OSP Conan remote |
| Julia | `Libcosim` | General registry, binaries as Pkg artifacts |
| Java | `libcosim-<v>.jar` | GitHub release |
| C# | `Libcosim` NuGet package | GitHub release |

The Python, C and Julia bindings are maintained by the libcosim team; Java
and C# are best effort. Times are `int64` nanoseconds in all languages, with
helpers for seconds. See [docs/](docs/) for getting-started guides per
language.

## Layout

```
cosim.i                 shared interface: names, ownership, what is exposed
python/ c/ java/ csharp/  per-language interface (typemaps, errors) and build
julia/                  Julia package (on the C library), its generator
cmake/                  runtime bundling for packages, keyword check
packaging/              package builds, release merge, cibuildwheel setup
test_package/           Conan test of the C package
docs/                   user guides; the spike report (history)
```

## Building

In libcosim's build (Conan option `swig_bindings`, plus `swig_java` and
`swig_csharp`), the bindings and their tests (`ctest -R swig_`) build
together with libcosim; this is what libcosim's binding CI does.

Standalone, against the libcosim Conan package (this is how the packages
are built):

```sh
conan install . -of build -s build_type=Release --build=missing
source build/build/Release/generators/conanbuild.sh
cmake -S . -B build/cmake -DCMAKE_TOOLCHAIN_FILE=build/build/Release/generators/conan_toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release -DLIBCOSIM_SWIG_TEST_DATA_DIR=<libcosim>/tests/data
cmake --build build/cmake -j && ctest --test-dir build/cmake
```

Options: `LIBCOSIM_SWIG_PYTHON`, `_C`, `_JULIA` (default on), `_JAVA`,
`_CSHARP` (default off; Java needs a JDK, e.g. `-o "&:java=True"` with the
command above, C# the .NET SDK). Packages: see
[packaging/README.md](packaging/README.md).

## Checks

- **C API drift:** the generated C header is compared with
  `c/reference/cosim_c_wrap.h` (test `swig_c_api_reference`). After an
  intended change to the C API, run the target `update_swig_c_api_reference`
  and commit the new header.
- **Keyword renames:** the build fails if SWIG had to rename an identifier
  because it is a keyword in one of the languages (target
  `swig_keyword_check`); add a shared `%rename` in `cosim.i` instead.
- SWIG is pinned (4.4.1): the C backend is experimental, and a new SWIG
  version may change the generated C API.
