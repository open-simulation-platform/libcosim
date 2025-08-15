# libcosim - An open-source co-simulation library for C++

[![libcosim CI Conan](https://github.com/open-simulation-platform/libcosim/actions/workflows/ci-conan.yml/badge.svg)](https://github.com/open-simulation-platform/libcosim/actions/workflows/ci-conan.yml)
[![libcosim CI CMake](https://github.com/open-simulation-platform/libcosim/actions/workflows/ci-cmake.yml/badge.svg)](https://github.com/open-simulation-platform/libcosim/actions/workflows/ci-cmake.yml)

Libcosim was developed as part of the [Open Simulation Platform][OSP] (OSP).
See [`CONTRIBUTING.md`] for contributor guidelines and [`LICENSE`] for terms
of use.


## How to use

The recommended way to include libcosim in your own projects is via the
[Conan] C/C++ package manager. First, you need to add the OSP package
repository to your remotes, like this:

    conan remote add osp https://osp.jfrog.io/artifactory/api/conan/conan-local

Then, you can simply list `libcosim/<version>` as a requirement in your
conanfile. Note that libcosim requires Conan 2.x.

Even if your project cannot use Conan for some reason, it is still possible to
use libcosim. In that case, you have to build it from source and manage the
dependency yourself. Instructions for building are in the next section.

For demonstrations of libcosim use, check out the OSP [Cosim] command-line
interface and [Cosim Demo Application].

### Other programming languages

Wrappers exist for other programming languages too:

* C++: [libcosimc]
* Python: [libcosimpy]
* Java/JVM: [cosim4j]

Note that none of them have complete support for all libcosim features.


## How to build

### Step 0: Get your tools in order

The tools needed to build libcosim are:

  * Compilers: [Visual Studio] >= 16.0/2019 (Windows), GCC >= 9 (Linux)
  * Build tool: [CMake] >= 3.19
  * Package manager (optional): [Conan] 2.x
  * API documentation generator (optional): [Doxygen]

In this guide, we will use Conan to download and configure libcosim's
dependencies. We strongly recommend that you do too, even when you're unable
to use it to incorporate libcosim in your own project afterwards. It *is*
possible to use other package managers or manage the dependencies yourself,
but then you're mostly on your own. (See the [CI without Conan] GitHub
actions workflow we've set up for an example of how to do it via the Debian
package manager.)

We will also assume that your CMake is version 3.23 or newer, even if
libcosim's minimum requirement is CMake 3.19. The reason is that this lets us
use the convenient `--preset` switch. If you need to use a slightly older
version, the [Conan CMakeToolchain documentation] has a simple workaround.

### Step 1: Install dependencies

First, make sure that you've added the OSP package repository to your Conan
remotes as shown in the previous section. Then, go to the root directory of
the libcosim source tree (i.e., the directory that contains this README
file) and run the following command:

    conan install --build=missing .

This will install all dependencies, building the ones for which binary
packages weren't available online, and set up some configuration files for the
next steps. Add `--options="proxyfmu=True"` to the above command to enable
support for [proxy-fmu].

### Step 2: Generate build system

Next, to generate the libcosim build system, run:

    cmake --preset=<config-preset-name>

where `<config-preset-name>` depends on the underlying toolchain. For Visual
Studio on Windows it will usually be `conan-default`, while for GCC/Make on
Linux it should be `conan-release` or `conan-debug`, depending on which build
type you want. (More precisely, it depends on whether a multi- or
single-configuration CMake generator is used. Consult the
[Conan CMakeToolchain documentation] for details on presets and their names.)

Note that the build type you choose must match one set up by `conan install`.
If you only ran `conan install` once and didn't specify a build type in the
first step, it will pick the default one from your Conan profile (usually
"release"). You can re-run `conan install` with different `build_type` settings
if you want to be able to easily switch between different build types.

There are a few options you may want to add to this command to enable or
disable various features for which we do not have corresponding Conan options.
The only one we'll mention here is `-DLIBCOSIM_BUILD_TESTS=ON`, which will
enable running the test suite in step 4. For other options, see the `option()`
declarations near the top of [CMakeLists.txt], or use the CMake GUI.

### Step 3: Build libcosim

To build libcosim, now run

    cmake --build --preset=<build-preset-name>

where `<build-preset-name>` will usually be `conan-release` or `conan-debug`,
depending again on which build type you want. (Note that on Windows/VS this is
*different* from the previous step, while on Linux/GCC/Make it must be the
exact *same*.)

### Step 4 (optional): Run tests

If you did not enable unittests in step 2, go back and do so now, and rebuild
libcosim. Then, run

    ctest --preset=<build-preset-name>

where `<build-preset-name>` is the same as in the previous step.

### Step 5 (optional): Install libcosim

Before you do this, you probably want to set the [`CMAKE_INSTALL_PREFIX`]
variable to the desired install location first. Then, simply run:

    cmake --build --preset=<build-preset-name> --target=install


[CI without Conan]: ./.github/workflows/ci-cmake.yml
[CMake]: https://cmake.org
[`CMAKE_INSTALL_PREFIX`]: https://cmake.org/cmake/help/latest/variable/CMAKE_INSTALL_PREFIX.html
[CMakeLists.txt]: ./CMakeLists.txt
[Conan]: https://conan.io
[Conan CMakeToolchain documentation]: https://docs.conan.io/2/examples/tools/cmake/cmake_toolchain/build_project_cmake_presets.html
[`CONTRIBUTING.md`]: ./CONTRIBUTING.md
[Cosim]: https://github.com/open-simulation-platform/cosim-cli
[Cosim Demo Application]: https://github.com/open-simulation-platform/cosim-demo-app
[cosim4j]: https://github.com/open-simulation-platform/cosim4j
[Doxygen]: http://www.doxygen.org
[libcosimc]: https://github.com/open-simulation-platform/libcosimc
[libcosimpy]: https://github.com/open-simulation-platform/libcosimpy
[`LICENSE`]: ./LICENSE
[OSP]: https://opensimulationplatform.com/
[proxy-fmu]: https://github.com/open-simulation-platform/proxy-fmu/
[Visual Studio]: https://visualstudio.microsoft.com
