import os

from conans import ConanFile, CMake


class CSECoreConan(ConanFile):
    name = "cse-core"
    version = "0.2.0"
    author = "osp"
    scm = {
        "type": "git",
        "url": "git@github.com:open-simulation-platform/cse-core.git",
        "revision": "auto"
    }
    settings = "os", "compiler", "build_type", "arch"
    generators = "cmake", "virtualrunenv"
    requires = (
        "boost/1.66.0@conan/stable",
        "FMILibrary/2.0.3@kyllingstad/testing",
        "gsl_microsoft/1.0.0@bincrafters/stable",
        "libevent/2.0.22@bincrafters/stable",
        "libzip/1.5.1@bincrafters/stable",
        "jsonformoderncpp/3.5.0@vthiery/stable"
        )

    options = {"fmuproxy": [True, False]}
    default_options = (
        "fmuproxy=False",
        "boost:shared=True",
        "libevent:with_openssl=False",
        "libzip:shared=True"
        )
    
    def imports(self):
        binDir = os.path.join("output", str(self.settings.build_type).lower(), "bin")
        self.copy("*.dll", dst=binDir, keep_path=False)
        self.copy("*.pdb", dst=binDir, keep_path=False)

    def requirements(self):
        if self.options.fmuproxy:
            self.requires("OpenSSL/1.0.2o@conan/stable")
            self.requires("thrift/0.12.0@helmesjo/stable")

    def configure_cmake(self):
        cmake = CMake(self)
        cmake.parallel = False # Needed to keep stable build on Jenkins Windows Node
        cmake.definitions["CSECORE_USING_CONAN"] = "ON"
        if self.settings.build_type == "Debug":
            cmake.definitions["CSECORE_BUILD_PRIVATE_APIDOC"] = "ON"
        if self.options.fmuproxy:
            cmake.definitions["CSECORE_WITH_FMUPROXY"] = "ON"
        cmake.configure()
        return cmake

    def build(self):
        cmake = self.configure_cmake()
        cmake.build()
        cmake.test()
        self.run('cmake --build . --target doc')

    def package(self):
        cmake = self.configure_cmake()
        self.run('cmake --build %s --target install-doc' % (self.build_folder))
        cmake.install()
        