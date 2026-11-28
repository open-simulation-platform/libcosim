# Building the documentation

The documentation combines Doxygen HTML and XML with a Sphinx guide. Breathe
uses the XML to include the C++ API reference in the guide. The guide sources
are reStructuredText.
The Design section explains the library stack and its submodules. The FMI 3
sidebar groups the overview, usage and implemented features; expand the
feature reference to browse individual topics and public API examples.
The Execution and algorithms group contains dedicated fixed-step and ECCO
guides; the latter illustrates ECCO's adaptive loop and power bonds.
FMI 3 feature pages illustrate fixed-step event boundaries, coupled Event
Mode and Clock/data routing alongside their relevant restrictions. Model and
connection diagrams identify the systems used by the execution timelines.

The linked workflow figure and other diagrams are hand-authored SVG files in
`docs/_static/figures`. They are embedded as HTML objects with text and
ordinary documentation links alongside them. SVG links are relative to the
figure's location in the generated `_static/figures` directory; when editing
a figure, verify its links against the generated HTML and check its layout at
narrow and wide viewport widths.

Run the commands below from the repository root.

## Prerequisites

- Python 3.10 or newer and [uv](https://docs.astral.sh/uv/).
- CMake 3.19 or newer and [Doxygen](https://www.doxygen.nl/) for a
  documentation-only build.
- For the Conan or full CMake build, a C++ toolchain and the library's build
  dependencies, as described in the [repository README](../README.md).

Create a virtual environment and install Sphinx, Breathe, and Furo with:

```shell
uv venv .venv
uv pip install --python .venv -r docs/requirements.txt
```

If `.venv` already exists, skip the first command. The requirements specify
compatible version ranges rather than locking transitive dependencies.
Do not share an environment between Windows and WSL/Linux: use a separate
checkout, or pass a platform-specific path to both `uv venv` and
`uv pip install --python`. If the environment is outside the repository, put
its `Scripts` directory on `PATH` on Windows or its `bin` directory on Linux.
When another `sphinx-build` is cached or present in the repository's `.venv`,
set `LIBCOSIM_SPHINX_BUILD` to the chosen executable when configuring CMake.

## Build documentation only

The standalone CMake project generates the same Doxygen and Sphinx output
without Conan, a C++ compiler, or the library's dependencies. Install Doxygen
and make it available on `PATH`, then run:

```shell
cmake -S docs -B build/docs-only
cmake --build build/docs-only --target doc --parallel
```

The guide is in `build/docs-only/output/sphinx` and Doxygen HTML is in
`build/docs-only/output/doc/html`. The CI documentation job uses this build
and uploads both output trees.

The FMI 3 examples and integration tests refer to the fixtures under
`tests/data/fmi3`. CTest passes that directory as `REFERENCE_FMU_DIR`.
The separate [fixture generator](../tests/build-test-fmus/README.md)
pins upstream Reference FMUs `v0.0.42`; building the documentation does
not regenerate or modify those fixtures.

## Build with Conan

The `docs` option builds the complete Doxygen and Sphinx documentation,
including the internal API reference. Conan supplies
Doxygen as a build tool, so a separate system installation is not needed for
`conan build`. Its package excludes Doxygen's optional external search tools;
the generated HTML still uses client-side search. The commands below use
separate Windows and Linux build trees and separate Conan user presets. On
first use, run `conan profile detect` if no default Conan profile exists.

### Windows PowerShell

```powershell
$preset = "tools.cmake.cmaketoolchain:user_presets=build\docs-windows\CMakeUserPresets.json"
conan build . --output-folder=build\docs-windows --build=missing -s build_type=Release -o "&:docs=True" -c $preset
```

Open `build\docs-windows\build\output\sphinx\index.html` for the guide or
`build\docs-windows\build\output\doc\html\index.html` for Doxygen.

### Linux or WSL

```bash
preset="tools.cmake.cmaketoolchain:user_presets=build/docs-linux/CMakeUserPresets.json"
conan build . --output-folder=build/docs-linux --build=missing -s build_type=Release -o "&:docs=True" -c "$preset"
```

Open `build/docs-linux/build/Release/output/sphinx/index.html` for the guide
or `build/docs-linux/build/Release/output/doc/html/index.html` for Doxygen.

## Build with the full CMake project

For an existing build tree with libcosim's dependencies configured, make
[Doxygen](https://www.doxygen.nl/) available on `PATH` and configure with
`LIBCOSIM_BUILD_DOCS=ON`. The option defaults to `OFF`. A normal build then
generates all documentation, or the `doc` target can be built separately:

```shell
cmake --build <build-directory> --config Release --target doc --parallel
```

The `doc` target generates Doxygen HTML and XML and the Sphinx guide, including
internal interfaces. The Sphinx guide is in `<build-directory>/output/sphinx`,
and Doxygen HTML is in `<build-directory>/output/doc/html`. Set
`LIBCOSIM_SPHINX_BUILD` to the `sphinx-build` executable if CMake cannot find
it in `.venv` or on `PATH`. Sphinx builds treat warnings as errors; Doxygen may
report undocumented-member warnings. The former individual documentation
options are ignored; replace them with `LIBCOSIM_BUILD_DOCS`.

## Install

Build the documentation before installing when it is enabled. Run
`cmake --install <build-directory> --config Release --prefix <install-prefix>`
to install the library and documentation, or build the `install-doc` target
to copy documentation to the configured `CMAKE_INSTALL_PREFIX`.

`cmake --install` puts the documentation in `doxygen` and `sphinx` directories
under the library documentation directory. The `install-doc` target retains
its existing layout: Doxygen's `html` and `xml` directories are directly
under the library documentation directory, alongside `sphinx`. Documentation
targets recreate their generated output directories on every build;
installation does not remove old files from an existing prefix.
