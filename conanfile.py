import os

from conan import ConanFile
from conan.errors import ConanInvalidConfiguration
from conan.tools.cmake import CMakeToolchain, CMake, CMakeDeps, cmake_layout
from conan.tools.env import VirtualRunEnv
from conan.tools.files import copy, load


class LibcosimConan(ConanFile):
    # Basic package info
    name = "libcosim"

    def set_version(self):
        self.version = load(
            self, os.path.join(self.recipe_folder, "version.txt")
        ).strip()

    # Metadata
    license = "MPL-2.0"
    author = "osp"
    url = "https://github.com/open-simulation-platform/libcosim.git"
    description = "A co-simulation library for C++"

    # Binary configuration
    package_type = "library"
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "shared": [True, False],
        "fPIC": [True, False],
        "proxyfmu": [True, False],
        "no_fmi_logging": [True, False],
        "swig_bindings": [True, False],
        "swig_java": [True, False],
    }
    default_options = {
        "shared": True,
        "fPIC": True,
        "proxyfmu": False,
        "no_fmi_logging": False,
        "swig_bindings": False,
        "swig_java": False,
    }

    # Dependencies/requirements
    def requirements(self):
        self.tool_requires("cmake/[>=4.0]")
        self.requires("fmilibrary/[~3.0.4]")
        self.requires("libcbor/0.11.0")
        self.requires("libzip/[~1.11]")
        self.requires("ms-gsl/[>=3 <5]", transitive_headers=True)
        self.requires("boost/[~1.85]", transitive_headers=True, transitive_libs=True)  # Required by Thrift
        if self.options.proxyfmu:
            self.requires("proxyfmu/0.4.3@osp/testing",
                          transitive_headers=True,
                          transitive_libs=True)
        self.requires("yaml-cpp/[~0.9]")
        self.requires("xerces-c/[~3.2]")

    def build_requirements(self):
        if self.options.swig_bindings:
            self.tool_requires("swig/4.4.1")  # pinned: the C backend is experimental
            if self.options.swig_java:
                self.tool_requires("zulu-openjdk/21.0.11")  # javac and JNI headers

    # Exports
    exports = "version.txt"
    exports_sources = ("src/*", "include/*", "cmake/*", "data/*", "docs/*", "tests/*", "bindings/*", "CHANGELOG.md", "CMakeLists.txt",
                       "CONTRIBUTING.md", "LICENSE", "README.md", "version.txt")

    # Build steps

    def layout(self):
        cmake_layout(self)

    def config_options(self):
        if self.settings.os == "Windows":
            del self.options.fPIC

    def configure(self):
        if self.options.shared:
            self.options.rm_safe("fPIC")
        self.options["*"].shared = self.options.shared
        # libzip is only used internally (src/cosim/utility/zip.cpp), so link
        # it statically. As a DLL it is named zip.dll, which clashes with the
        # JVM's own zip.dll on Windows: a JVM loads its zip.dll first, and
        # cosim.dll then gets bound to it ("The specified procedure could not
        # be found"), so libcosim could not be used from Java at all.
        self.options["libzip"].shared = False

    def generate(self):
        # Copy dependencies to the folder where executables (tests, mainly)
        # will be placed, so it's easier to run them.
        outputdir = os.path.join(
            self.build_folder, "output", str(self.settings.build_type).lower()
        )
        bindir = os.path.join(outputdir, "bin")
        libdir = os.path.join(outputdir, "lib")
        for dep in self.dependencies.values():
            for depdir in dep.cpp_info.bindirs:
                copy(self, "*.dll", depdir, bindir, keep_path=False)
                copy(self, "*.pdb", depdir, bindir, keep_path=False)
                copy(self, "proxyfmu*", depdir, bindir, keep_path=False)
            if self.settings.os == "Macos":
                # The proxyfmu executable looks for its libraries in ../lib,
                # and DYLD_LIBRARY_PATH does not reliably survive into the
                # child processes it is started in.
                for depdir in dep.cpp_info.libdirs:
                    copy(self, "*.dylib", depdir, libdir, keep_path=False)

        # Generate CMake toolchain file
        tc = CMakeToolchain(self)
        tc.cache_variables["LIBCOSIM_BUILD_APIDOC"] = False
        tc.cache_variables["LIBCOSIM_BUILD_TESTS"] = self._is_tests_enabled()
        tc.cache_variables["LIBCOSIM_NO_FMI_LOGGING"] = self.options.no_fmi_logging
        tc.cache_variables["LIBCOSIM_WITH_PROXYFMU"] = self.options.proxyfmu
        tc.cache_variables["LIBCOSIM_BUILD_SWIG_BINDINGS"] = self.options.swig_bindings
        tc.cache_variables["LIBCOSIM_SWIG_JAVA"] = self.options.swig_bindings and self.options.swig_java
        tc.generate()
        CMakeDeps(self).generate()

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()
        if self._is_tests_enabled():
            env = VirtualRunEnv(self).environment()
            env.define("CTEST_OUTPUT_ON_FAILURE", "ON")
            with env.vars(self).apply():
                cmake.test()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["cosim"]
        self.cpp_info.set_property("cmake_target_name", "libcosim::cosim")
        # Tell consumers to use "our" CMake package config file.
        self.cpp_info.builddirs.append(".")
        self.cpp_info.set_property("cmake_find_mode", "none")

    def validate(self):
        if self.options.shared and not self.dependencies["boost"].options.shared:
            raise ConanInvalidConfiguration(
                "Option libcosim:shared=True also requires option boost:shared=True"
            )

    # Helper functions

    def _is_tests_enabled(self):
        return os.getenv("LIBCOSIM_RUN_TESTS_ON_CONAN_BUILD", "False").lower() in (
            "true",
            "1",
        )

