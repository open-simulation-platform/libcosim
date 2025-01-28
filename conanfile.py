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
        self.version = load(self, os.path.join(self.recipe_folder, "version.txt")).strip()

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
    }
    default_options = {
        "shared": True,
        "fPIC": True,
        "proxyfmu": False,
        "no_fmi_logging": False,
    }

    # Dependencies/requirements
    def requirements(self):
        self.tool_requires("cmake/[>=3.19]")
        self.requires("fmilibrary/[~2.3]")
        self.requires("libcbor/[~0.11.0]")
        self.requires("libzip/[>=1.7 <1.10]") # 1.10 deprecates some functions we use
        self.requires("ms-gsl/[>=3 <5]", transitive_headers=True)
        if self.options.proxyfmu:
            self.requires("proxyfmu/0.3.2@osp/stable")
            self.requires("boost/[~1.81]", transitive_headers=True, transitive_libs=True) # Required by Thrift
        else:
            self.requires("boost/[>=1.71]", transitive_headers=True, transitive_libs=True)
        self.requires("yaml-cpp/[~0.8]")
        self.requires("xerces-c/[~3.2]")

    # Exports
    exports = "version.txt"
    exports_sources = "*"

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
        self.options["libcbor/*"].shared = False

    def generate(self):
        # Copy dependencies to the folder where executables (tests, mainly)
        # will be placed, so it's easier to run them.
        bindir = os.path.join(
            self.build_folder,
            "output",
            str(self.settings.build_type).lower(),
            "bin")
        for dep in self.dependencies.values():
            for depdir in dep.cpp_info.bindirs:
                copy(self, "*.dll", depdir, bindir, keep_path=False)
                copy(self, "*.pdb", depdir, bindir, keep_path=False)
                copy(self, "proxyfmu*", depdir, bindir, keep_path=False)

        # Generate CMake toolchain file
        tc = CMakeToolchain(self)
        tc.cache_variables["LIBCOSIM_BUILD_APIDOC"] = False
        tc.cache_variables["LIBCOSIM_BUILD_TESTS"] = self._is_tests_enabled()
        tc.cache_variables["LIBCOSIM_NO_FMI_LOGGING"] = self.options.no_fmi_logging
        tc.cache_variables["LIBCOSIM_WITH_PROXYFMU"] = self.options.proxyfmu
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
            raise ConanInvalidConfiguration("Option libcosim:shared=True also requires option boost:shared=True")

    # Helper functions

    def _is_tests_enabled(self):
        return os.getenv("LIBCOSIM_RUN_TESTS_ON_CONAN_BUILD", "False").lower() in ("true", "1")
