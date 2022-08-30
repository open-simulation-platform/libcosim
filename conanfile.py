import os

from conans import ConanFile, CMake, RunEnvironment, tools
from os import path


class LibcosimConan(ConanFile):
    name = "libcosim"
    author = "osp"
    exports = "version.txt"
    scm = {
        "type": "git",
        "url": "auto",
        "revision": "auto"
    }
    settings = "os", "compiler", "build_type", "arch"
    generators = "cmake", "virtualrunenv"
    requires = (
        "boost/1.71.0",
        "fmilibrary/2.3",
        "ms-gsl/2.1.0",
        "libzip/1.7.3",
        "yaml-cpp/0.7.0",
        "xerces-c/3.2.2",
        # conflict resolution
        "openssl/1.1.1k",
        "xz_utils/5.2.5",
        "zlib/1.2.12"
    )

    options = {
        "shared": [True, False],
        "proxyfmu": [True, False],
        "no_fmi_logging": [True, False]}
    default_options = (
        "proxyfmu=False",
        "shared=True",
        "no_fmi_logging=False"
    )

    def is_tests_enabled(self):
        return os.getenv("LIBCOSIM_RUN_TESTS_ON_CONAN_BUILD", "False").lower() in ("true", "1")

    def set_version(self):
        self.version = tools.load(path.join(self.recipe_folder, "version.txt")).strip()

    def configure(self):
        self.options["boost"].shared = self.options.shared
        self.options["fmilibrary"].shared = self.options.shared
        self.options["libzip"].shared = self.options.shared
        self.options["yaml-cpp"].shared = self.options.shared
        self.options["xerces-c"].shared = self.options.shared

    def requirements(self):
        if self.options.proxyfmu:
            self.requires("proxyfmu/0.2.8@osp/testing-socket-open-timeout")

    def imports(self):
        binDir = os.path.join("output", str(self.settings.build_type).lower(), "bin")
        self.copy("proxyfmu*", dst=binDir, src="bin", keep_path=False)
        self.copy("proxyfmu*", dst="tests", src="bin", keep_path=False)
        self.copy("*.dll", dst=binDir, keep_path=False)
        self.copy("*.pdb", dst=binDir, keep_path=False)

    def configure_cmake(self):
        cmake = CMake(self)
        cmake.definitions["LIBCOSIM_USING_CONAN"] = "ON"
        cmake.definitions["LIBCOSIM_BUILD_APIDOC"] = "OFF"
        cmake.definitions["LIBCOSIM_BUILD_TESTS"] = self.is_tests_enabled()
        cmake.definitions["BUILD_SHARED_LIBS"] = self.options.shared
        cmake.definitions["LIBCOSIM_NO_FMI_LOGGING"] = self.options.no_fmi_logging
        if self.options.proxyfmu:
            cmake.definitions["LIBCOSIM_WITH_PROXYFMU"] = "ON"
        cmake.configure()
        return cmake

    def build(self):
        cmake = self.configure_cmake()
        cmake.build()
        if self.is_tests_enabled():
            env_run = RunEnvironment(self)
            with tools.environment_append(env_run.vars):
                cmake.test(output_on_failure=True)


    def package(self):
        cmake = self.configure_cmake()
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["cosim"]
