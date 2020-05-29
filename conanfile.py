import os

from conans import ConanFile, CMake, tools
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
        "libzip/1.5.2@bincrafters/stable",
        "openssl/1.0.2u",
        "zlib/1.2.11",
        "yaml-cpp/0.6.3",
        "bzip2/1.0.8",
        "xerces-c/3.2.2"
    )

    options = {"fmuproxy": [True, False]}
    default_options = (
        "fmuproxy=False",
        "boost:shared=True"
    )

    def set_version(self):
        self.version = tools.load(path.join(self.recipe_folder, "version.txt")).strip()

    def imports(self):
        binDir = os.path.join("output", str(self.settings.build_type).lower(), "bin")
        self.copy("*.dll", dst=binDir, keep_path=False)
        self.copy("*.pdb", dst=binDir, keep_path=False)

    def requirements(self):
        if self.options.fmuproxy:
            self.requires("thrift/0.12.0@bincrafters/stable")

    def build_requirements(self):
        if self.options.fmuproxy:
            self.build_requires("thrift_installer/0.12.0@bincrafters/stable")

    def configure_cmake(self):
        cmake = CMake(self)
        cmake.definitions["LIBCOSIM_USING_CONAN"] = "ON"
        cmake.definitions["LIBCOSIM_BUILD_APIDOC"] = "OFF"
        cmake.definitions["LIBCOSIM_BUILD_TESTS"] = "OFF"
        if self.options.fmuproxy:
            cmake.definitions["LIBCOSIM_WITH_FMUPROXY"] = "ON"
        cmake.configure()
        return cmake

    def build(self):
        cmake = self.configure_cmake()
        cmake.build()

    def package(self):
        cmake = self.configure_cmake()
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["cosim"]
