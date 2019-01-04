import os

from conans import ConanFile, CMake


class CSECoreConan(ConanFile):
    name = "cse-core"
    version = "0.1.0"
    author = "osp"
    scm = {
        "type": "git",
        "url": "auto",
        "revision": "auto"
    }
    settings = "os", "compiler", "build_type", "arch"
    generators = "cmake", "virtualrunenv"
    requires = (
        "boost/1.66.0@conan/stable",
        "FMILibrary/2.0.3@kyllingstad/testing",
        "gsl_microsoft/1.0.0@bincrafters/stable",
        "libevent/2.0.22@bincrafters/stable",
        "libzip/1.5.1@bincrafters/stable"
        )
    options = {"ci": [True, False]}
    default_options = (
        "ci=False",
        "boost:shared=True",
        "libevent:with_openssl=False",
        "libzip:shared=True"
        )
    
    def imports(self):
        binDir = os.path.join("output", str(self.settings.build_type).lower(), "bin")
        self.copy("*.dll", dst=binDir, keep_path=False)
        self.copy("*.pdb", dst=binDir, keep_path=False)

    def build(self):
        cmake = CMake(self)
        cmake.definitions["CSECORE_USING_CONAN"] = "ON"
        if self.options.ci:
            cmake.parallel = False
            cmake.definitions["CSCSECORE_BUILD_PRIVATE_APIDOC"] = "ON"
        cmake.configure()
        cmake.build()
        cmake.test()
        self.run('cmake --build . --target doc')

    def package(self):
        cmake = CMake(self)
        cmake.install()
