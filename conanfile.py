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
        "fmilibrary/2.0.3",
        "ms-gsl/2.1.0",
        "libzip/1.7.3",
        "yaml-cpp/0.6.3",
        "xerces-c/3.2.2",
        # conflict resolution
        "openssl/1.1.1k",
        "xz_utils/5.2.5"
    )

    options = {"fmuproxy": [True, False]}
    default_options = (
        "fmuproxy=False",
        "boost:shared=True",
        "fmilibrary:shared=True",
        "libzip:shared=True",
        "yaml-cpp:shared=True",
        "xerces-c:shared=True"
    )

    def is_tests_enabled(self):
        return os.getenv("LIBCOSIM_RUN_TESTS_ON_CONAN_BUILD", "False").lower() in ("true", "1")

    def set_version(self):
        self.version = tools.load(path.join(self.recipe_folder, "version.txt")).strip()

    def imports(self):
        binDir = os.path.join("output", str(self.settings.build_type).lower(), "bin")
        self.copy("*.dll", dst=binDir, keep_path=False)
        self.copy("*.pdb", dst=binDir, keep_path=False)

    def requirements(self):
        if self.options.fmuproxy:
            self.requires("thrift/0.13.0")

    def configure_cmake(self):
        cmake = CMake(self)
        cmake.definitions["LIBCOSIM_USING_CONAN"] = "ON"
        cmake.definitions["LIBCOSIM_BUILD_APIDOC"] = "OFF"
        cmake.definitions["LIBCOSIM_BUILD_TESTS"] = self.is_tests_enabled()
        if self.options.fmuproxy:
            cmake.definitions["LIBCOSIM_WITH_FMUPROXY"] = "ON"
            cmake.definitions["LIBCOSIM_TEST_FMUPROXY"] = "OFF" # Temporary, to be removed again in PR #633
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
