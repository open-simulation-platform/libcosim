import os

from conans import ConanFile, CMake


class CSECoreConan(ConanFile):
    name = "cse-core"
    version = "0.6.0"
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
        "gsl_microsoft/20171020@bincrafters/stable",
        "libzip/1.5.2@bincrafters/stable",
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

    def imports(self):
        binDir = os.path.join("output", str(self.settings.build_type).lower(), "bin")
        self.copy("*.dll", dst=binDir, keep_path=False)
        self.copy("*.pdb", dst=binDir, keep_path=False)

    def requirements(self):
        if self.options.fmuproxy:
            self.requires("thrift/0.12.0@bincrafters/stable")

    def configure_cmake(self):
        cmake = CMake(self)
        cmake.parallel = False # Needed to keep stable build on Jenkins Windows Node
        cmake.definitions["CSECORE_USING_CONAN"] = "ON"
        if self.settings.build_type == "Debug":
            cmake.definitions["CSECORE_BUILD_PRIVATE_APIDOC"] = "ON"
        if self.options.fmuproxy:
            cmake.definitions["CSECORE_WITH_FMUPROXY"] = "ON"
            cmake.definitions["CSECORE_TEST_FMUPROXY"] = "OFF" # since we can't test on Jenkins yet
        cmake.configure()
        return cmake

    def build(self):
        cmake = self.configure_cmake()
        cmake.build()
        self.run('cmake --build . --target doc')

    def package(self):
        cmake = self.configure_cmake()
        self.run('cmake --build %s --target install-doc' % (self.build_folder))
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = [ "csecorecpp", "csecorec" ]
