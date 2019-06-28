import os

from conans import ConanFile, CMake


class CSECoreConan(ConanFile):
    name = "cse-core"
    version = "0.3.0"
    author = "osp"
    description = "CSE co-simulation core"
    url = "https://github.com/open-simulation-platform/cse-core"
    settings = "os", "compiler", "build_type", "arch"
    generators = "cmake", "virtualrunenv"
    requires = (
        "boost/1.66.0@conan/stable",
        "FMILibrary/2.0.3@kyllingstad/testing",
        "gsl_microsoft/1.0.0@bincrafters/stable",
        "libzip/1.5.1@bincrafters/stable",
        "jsonformoderncpp/3.5.0@vthiery/stable"
        )

    options = {
        "revision": "ANY",
        "build_apidoc": [True, False],
        "fmuproxy": [True, False]
    }
    default_options = (
        "revision=master",
        "build_apidoc=True",
        "fmuproxy=False",
        "boost:shared=True",
        "libzip:shared=True"
        )
        
    def source(self):
        self.run("git clone git@github.com:open-simulation-platform/cse-core.git")
        self.run("cd cse-core && git checkout " + str(self.options.revision))

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
        cmake.definitions["CSECORE_BUILD_APIDOC"] = "ON" if self.options.build_apidoc else "OFF"
        if self.options.build_apidoc and self.settings.build_type == "Debug":
            cmake.definitions["CSECORE_BUILD_PRIVATE_APIDOC"] = "ON"
        if self.options.fmuproxy:
            cmake.definitions["CSECORE_WITH_FMUPROXY"] = "ON"
            cmake.definitions["CSECORE_TEST_FMUPROXY"] = "OFF" # since we can't test on Jenkins yet
        cmake.configure(source_folder = "cse-core")
        return cmake

    def build(self):
        cmake = self.configure_cmake()
        cmake.build()
        cmake.test()
        if not self.options.build_apidoc:
            self.run('cmake --build .')
        else:
            self.run('cmake --build . --target doc')
            
    def package(self):
        cmake = self.configure_cmake()
        if not self.options.build_apidoc:
            self.run('cmake --build %s' % (self.build_folder))
        else:
            self.run('cmake --build %s --target install-doc' % (self.build_folder))
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = [ "csecorecpp", "csecorec" ]
