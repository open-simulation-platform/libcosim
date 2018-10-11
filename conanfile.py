import os

from conans import ConanFile, CMake, tools


class CSECoreConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "cmake"
    requires = (
        "boost_algorithm/1.66.0@bincrafters/stable",
        "boost_fiber/1.66.0@bincrafters/stable",
        "boost_log/1.66.0@bincrafters/stable",
        "boost_property_tree/1.66.0@bincrafters/stable",
        "boost_test/1.66.0@bincrafters/stable",
        "boost_uuid/1.66.0@bincrafters/stable",
        "cmake_findboost_modular/1.66.0@bincrafters/stable",
        "FMILibrary/2.0.3@kyllingstad/testing",
        "gsl_microsoft/1.0.0@bincrafters/stable",
        "libevent/2.0.22@bincrafters/stable",
        "libzip/1.5.1@bincrafters/stable"
        )
    default_options = (
        "libevent:with_openssl=False",
        "libzip:shared=True"
        )

    def imports(self):
        binDir = os.path.join("output", str(self.settings.build_type).lower(), "bin")
        libDir = os.path.join("output", str(self.settings.build_type).lower(), "lib")
        self.copy("*.dll", dst=binDir, keep_path=False)
        self.copy("*.pdb", dst=binDir, keep_path=False)
        self.copy("*.so*", dst=libDir, keep_path=False)
