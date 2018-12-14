import os

from conans import ConanFile, CMake, tools


class CSECoreConan(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "cmake", "virtualrunenv"
    requires = (
        "boost/1.66.0@conan/stable",
        "FMILibrary/2.0.3@kyllingstad/testing",
        "gsl_microsoft/1.0.0@bincrafters/stable",
        "libevent/2.0.22@bincrafters/stable",
        "libzip/1.5.1@bincrafters/stable"
        )
    default_options = (
        "boost:shared=True",
        "libevent:with_openssl=False",
        "libzip:shared=True"
        )

    def imports(self):
        binDir = os.path.join("output", str(self.settings.build_type).lower(), "bin")
        self.copy("*.dll", dst=binDir, keep_path=False)
        self.copy("*.pdb", dst=binDir, keep_path=False)
