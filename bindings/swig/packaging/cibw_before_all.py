"""
cibuildwheel `before-all` step: provides libcosim and SWIG through Conan and
writes the CMake toolchain the wheel builds use.

    python cibw_before_all.py <repository root>

Runs once per platform (on Linux inside the manylinux container, so libcosim
and its dependencies are built for that glibc where no compatible binary
exists). The toolchain ends up in <root>/build-conan/build/[Release/]generators/;
the wheel build gets it with
    cmake.define.CMAKE_TOOLCHAIN_FILE=<that dir>/conan_toolchain.cmake

LIBCOSIM_FROM_SOURCE=1 first creates the libcosim package from <root> (as
libcosim/<v>@spike/testing, for unreleased libcosim versions); otherwise
libcosim comes from the OSP Conan remote (LIBCOSIM_REF in conanfile.py).
"""
import os
import subprocess
import sys
import sysconfig

OSP_REMOTE = "https://osp.jfrog.io/artifactory/api/conan/conan-local"


def run(*cmd):
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True)


def main(root):
    run(sys.executable, "-m", "pip", "install", "-q", "conan")
    conan = os.path.join(sysconfig.get_path("scripts"), "conan")
    run(conan, "profile", "detect", "--force")
    run(conan, "remote", "add", "osp", OSP_REMOTE, "--force")
    if os.environ.get("LIBCOSIM_FROM_SOURCE") == "1":
        run(conan, "create", root, "--user", "spike", "--channel", "testing",
            "-s", "build_type=Release", "--build=missing")
    run(conan, "install", os.path.join(root, "bindings", "swig"),
        "-of", os.path.join(root, "build-conan"), "-s", "build_type=Release", "--build=missing")


if __name__ == "__main__":
    main(os.path.abspath(sys.argv[1]))
