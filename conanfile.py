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
        "libzip/1.7.3",
        "yaml-cpp/0.6.3",
        "xerces-c/3.2.2",
        # conflict resolution
        "openssl/1.1.1k"
    )

    options = {"proxyfmu": [True, False]}
    default_options = (
        "proxyfmu=False",
        "boost:shared=True",
        "fmilibrary:shared=True",
        "libzip:shared=True",
        "yaml-cpp:shared=True",
        "xerces-c:shared=True"
    )

    def set_version(self):
        self.version = tools.load(path.join(self.recipe_folder, "version.txt")).strip()

    def requirements(self):
        if self.options.proxyfmu:
            self.requires("proxyfmu/0.2.1@osp/testing")

    def imports(self):
        binDir = os.path.join("output", str(self.settings.build_type).lower(), "bin")
        self.copy("proxyfmu.exe", dst=binDir, src="bin", keep_path=False)
        self.copy("*.dll", dst=binDir, keep_path=False)
        self.copy("*.pdb", dst=binDir, keep_path=False)

    def configure_cmake(self):
        cmake = CMake(self)
        cmake.definitions["LIBCOSIM_USING_CONAN"] = "ON"
        cmake.definitions["LIBCOSIM_BUILD_APIDOC"] = "OFF"
        cmake.definitions["LIBCOSIM_BUILD_TESTS"] = "OFF"
        if self.options.proxyfmu:
            cmake.definitions["LIBCOSIM_WITH_PROXYFMU"] = "ON"
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
