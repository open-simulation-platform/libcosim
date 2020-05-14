libcosim - A co-simulation library for C++
==========================================

This repository contains two libraries:

  * A C++ library for distributed co-simulations.
  * A C library which wraps and exposes a subset of the C++ library's functions.

See [`CONTRIBUTING.md`] for contributor guidelines and [`LICENSE`] for
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

First, add the OSP Conan repository as a remote and configure the username and
password to access it:

    conan remote add osp https://osp-conan.azurewebsites.net/artifactory/api/conan/public
    conan user -p "Open Simulation Platform" -r osp osp

As we will build the library using the *debug* configuration in this guide (as
opposed to *release*), we must use the Conan setting `build_type=Debug`.  For
GCC, we also need to set `compiler.libcxx=libstdc++11`, because the library
makes heavy use of C++11/14/17 features.  You can either change these settings
in your [Conan profile], or you can specify them using the `--settings` switch
when running `conan install` later. To do the former, add one or both of the
following lines to the appropriate profile(s):

    build_type=Debug
    compiler.libcxx=libstdc++11

Again, the second line should only be added if you are compiling with GCC.


### Step 2: Prepare build system

Now, we will create a directory to hold the build system and generated files,
use Conan to acquire dependencies, and run CMake to generate the build system.

We'll create the build directory as a subdirectory of our root source
directory—that is, the directory which contains this README file—and call it
`build`.  Note, however, that it may be located anywhere and be called anything
you like.

From the libcosim source directory, create and enter the build directory:

    mkdir build
    cd build

Then, acquire dependencies with Conan:

    conan install .. --build=missing

(You may also have to append `--settings build_type=Debug` and possibly
`--settings compiler.libcxx=libstdc++11` to this command; see Step 1 for more
information.)

#### FMU-Proxy
To include FMU-Proxy support, run conan install with the additional option:
```bash
-o fmuproxy=True
```
Note that it currently must be built in Release mode e.g.
```bash
conan install .. -s build_type=Release --build=missing -o fmuproxy=True
```

Now, we can run CMake to generate the build system.  (If you have not installed
Doxygen at this point, append `-DLIBCOSIM_BUILD_APIDOC=OFF` to the next command
to disable API documentation generation.)

For **Visual Studio**, enter:

    cmake .. -DLIBCOSIM_USING_CONAN=TRUE -A x64

For **GCC**, run:

    cmake .. -DLIBCOSIM_USING_CONAN=TRUE -DCMAKE_BUILD_TYPE=Debug

At this point, we are ready to build and test the software.  But first, here are
some things worth noting about what we just did:

  * The `-A` (architecture) switch we used for Visual Studio ensures that we build
    in 64-bit mode, which is the default for Conan, but not for Visual Studio.
  * In addition to generating build files for MSBuild, CMake generates solution
    files for the Visual Studio IDE.  Open the `libcosim.sln` file with VS if you
    want to check it out.
  * For GCC, CMake normally uses a Makefile generator which, unlike Visual
    Studio, is a *single-configuration* generator.  Therefore, the choice of
    whether to build in debug or release mode has to be made at generation time,
    using the `CMAKE_BUILD_TYPE` variable.

### Step 3: Build and test

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
[`LICENSE`]: ./LICENSE
[Visual Studio]: https://visualstudio.microsoft.com
[CMake]: https://cmake.org
[Doxygen]: http://www.doxygen.org
[Conan]: https://conan.io
[vcpkg]: https://github.com/Microsoft/vcpkg
[FMI Library]: http://jmodelica.org
[kyllingstad/conan-fmilibrary]: https://github.com/kyllingstad/conan-fmilibrary
[Bincrafters' public-conan repository]: https://bintray.com/bincrafters/public-conan
[Conan profile]: https://docs.conan.io/en/latest/using_packages/using_profiles.html
