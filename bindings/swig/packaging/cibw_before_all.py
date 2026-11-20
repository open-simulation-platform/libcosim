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
libcosim/<v>@local/testing, for unreleased libcosim versions) and builds
against it; otherwise libcosim comes from the OSP Conan remote
(libcosim/<v>@osp/stable, or LIBCOSIM_REF; see conanfile.py).
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
        run(conan, "create", root, "--user", "local", "--channel", "testing",
            "-s", "build_type=Release", "--build=missing")
        with open(os.path.join(root, "version.txt"), encoding="utf-8") as f:
            os.environ["LIBCOSIM_REF"] = f"libcosim/{f.read().strip()}@local/testing"
    # On Linux, build libcosim in the manylinux container: the OSP remote's
    # Linux binaries are built against a newer glibc than manylinux allows.
    build = ["--build=missing"] + (["--build=libcosim/*"] if sys.platform.startswith("linux") else [])
    run(conan, "install", os.path.join(root, "bindings", "swig"),
        "-of", os.path.join(root, "build-conan"), "-s", "build_type=Release", *build)


if __name__ == "__main__":
    main(os.path.abspath(sys.argv[1]))
