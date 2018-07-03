CSE co-simulation core
======================

This repository contains two libraries:

  * A C++ library for distributed co-simulations.
  * A C library which wraps and exposes a subset of the C++ library's functions.

See [`CONTRIBUTING.md`] for contributor guidelines and [`LICENCE.txt`] for
terms of use.


Toolchain
---------

  * Language: C++17
  * Compilers: MSVC 14.1 (i.e., Visual Studio 2017), GCC 6.3
  * Build tool: CMake >= 3.9
  * API doc generator: Doxygen >= 1.8
  * Package manager: Conan (optional)


Directory structure
-------------------
This repository is organised as follows:

  * `include/`:     Header files that define the public API.
  * `src/cpp/`:     Source code for the C++ library.
  * `src/c/`:       Source code for the C library.
  * `tests/cpp/`:   Test suite for the C++ library.
  * `tests/c/`:     Test suite for the C library.
  * `cmake/`:       CMake scripts and other build system files.


[`CONTRIBUTING.md`]: ./CONTRIBUTING.md
[`LICENCE.txt`]: ./LICENCE.txt
