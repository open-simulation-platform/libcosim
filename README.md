CSE co-simulation core
======================

This repository contains two libraries:

  * A C++ library for distributed co-simulations.
  * A C library which wraps and exposes a subset of the C++ library's functions.

See [`CONTRIBUTING.md`] for contributor guidelines and [`LICENCE.txt`] for
terms of use.


How to build
------------

### Required tools

  * Compilers: [Visual Studio] >= 2017 (Windows), GCC >= 8 (Linux)
  * Build tool: [CMake] >= 3.9
  * API documentation generator (optional): [Doxygen] >= 1.8
  * Package manager (optional): [Conan] >= 1.7

Throughout this guide, we will use Conan to manage dependencies.  However, it
should be possible to use other package managers as well, such as [vcpkg], and
of course you can always build and install dependencies manually.

### Step 1: Configure Conan

First of all, ensure that Conan is aware of [Bincrafters' public-conan
repository], which contains many of the packages we'll need:

    conan remote add bincrafters https://api.bintray.com/conan/bincrafters/public-conan

As we will build the library using the *debug* configuration in this guide (as
opposed to *release*), we must use the Conan setting `build_type=Debug`.  For
GCC, we also need to set `compiler.libcxx=libstdc++11`, because the library
makes heavy use of C++11/14/17 features.  You can either change these settings
in your [Conan profile], or you must remember to append the following switches
to every `conan` command hereafter:

    -s build_type=Debug -s compiler.libcxx=libstdc++11

### Step 2: Prepare the FMI Library package for Conan

At the time of writing, there is no prebuilt Conan package available for [FMI
Library], nor are there any package repositories that host the recipe.  So for
this one package, we need to do the package creation ourselves.

Clone/download [kyllingstad/conan-fmilibrary] from GitHub and run the following
command from the topmost directory (the one that contains `conanfile.py`):

    conan create . kyllingstad/testing


### Step 3: Prepare build system

Now, we will create a directory to hold the build system and generated files,
use Conan to acquire dependencies, and run CMake to generate the build system.

We'll create the build directory as a subdirectory of our root source
directory—that is, the directory which contains this README file—and call it
`build`.  Note, however, that it may be located anywhere and be called anything
you like.

From the cse-core source directory, create and enter the build directory:

    mkdir build
    cd build

Then, acquire dependencies with Conan:

    conan install .. --build=missing

Now, we can run CMake to generate the build system.  For **Visual Studio**,
enter:

    cmake .. -DCSECORE_USING_CONAN=TRUE -G "Visual Studio 15 2017 Win64"

For **GCC**, run:

    cmake .. -DCSECORE_USING_CONAN=TRUE -DCMAKE_BUILD_TYPE=Debug

At this point, we are ready to build and test the software.  But first, here are
some things worth noting about what we just did:

  * The `-G` (generator) switch we used for Visual Studio ensures that we build
    in 64-bit mode, which is the default for Conan, but not for Visual Studio.
  * In addition to generating build files for MSBuild, CMake generates solution
    files for the Visual Studio IDE.  Open the `csecore.sln` file with VS if you
    want to check it out.
  * For GCC, CMake normally uses a Makefile generator which, unlike Visual
    Studio, is a *single-configuration* generator.  Therefore, the choice of
    whether to build in debug or release mode has to be made at generation time,
    using the `CMAKE_BUILD_TYPE` variable.

### Step 4: Build and test

When CMake generates IDE project files, as is the case for the Visual Studio
generator, the software can of course be built, and tests run, from within the
IDE.  Here, however, we will show how to do it from the command line.

The following three commands will build the software, test the software, and
build the API documentation, respectively:

    cmake --build .
    ctest -C Debug
    cmake --build . --target doc

(The `-C Debug` switch is only necessary on multi-configuration systems like
Visual Studio.)

All generated files can be found in the directory `build/output`.


[`CONTRIBUTING.md`]: ./CONTRIBUTING.md
[`LICENCE.txt`]: ./LICENCE.txt
[Visual Studio]: https://visualstudio.microsoft.com
[CMake]: https://cmake.org
[Doxygen]: http://www.doxygen.org
[Conan]: https://conan.io
[vcpkg]: https://github.com/Microsoft/vcpkg
[FMI Library]: http://jmodelica.org
[kyllingstad/conan-fmilibrary]: https://github.com/kyllingstad/conan-fmilibrary
[Bincrafters' public-conan repository]: https://bintray.com/bincrafters/public-conan
[Conan profile]: https://docs.conan.io/en/latest/using_packages/using_profiles.html
