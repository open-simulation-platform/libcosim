"""
Builds the libcosim binding packages from this directory, against the libcosim
Conan package, and optionally tests each one the way a user would use it.

    python packaging/build_packages.py [--out DIR] [--languages ...] [--test]
                                       [--test-data DIR] [--conan-arg ARG ...]

Produces in <out>/dist:
    python/libcosim-<v>-<tags>.whl   Python wheel (scikit-build-core)
    java/libcosim-<v>.jar            classes + native libraries
    csharp/Libcosim.<v>.nupkg        NuGet package with runtimes/<rid>/native
    julia/Libcosim/                  Julia package, Artifacts.toml for this platform
    julia/libcosim_c-<triplet>.tar.gz  its binaries (the `libcosim_c` artifact),
                                     to upload to the GitHub release
    c/                               C library + cosim_c.h (also: `conan create .`)

Requires Conan 2, CMake, a C++ compiler, and for the languages built: Python
with `build`, a JDK 21, the .NET SDK, Julia. With --test, each package is
tested with nothing but the package itself on the search paths.
"""
import argparse
import glob
import os
import pathlib
import re
import shutil
import subprocess
import sys
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
SWIG_DIR = os.path.dirname(HERE)
ALL = ["c", "python", "java", "csharp", "julia"]
RELEASES = "https://github.com/open-simulation-platform/libcosim/releases/download"
WINDOWS = sys.platform == "win32"


def log(msg):
    print(f"=== {msg}", flush=True)


def run(cmd, env=None, cwd=None, conan_env=None):
    """Runs cmd, inside the Conan build environment if conan_env is given."""
    if conan_env:
        if WINDOWS:
            # One string: cmd.exe does its own quote parsing (/s: strip the outer quotes).
            cmd = f'cmd /s /c ""{conan_env}.bat" >nul && {subprocess.list2cmdline(cmd)}"'
        else:
            cmd = ["bash", "-c", f'source "{conan_env}.sh" >/dev/null && exec "$@"', "_", *cmd]
    print("+", cmd if isinstance(cmd, str) else subprocess.list2cmdline(cmd), flush=True)
    subprocess.run(cmd, env=env, cwd=cwd, check=True)


def clean_env():
    """The environment for the tests: no paths to libcosim or its dependencies."""
    env = dict(os.environ)
    for var in ("LD_LIBRARY_PATH", "DYLD_LIBRARY_PATH", "PYTHONPATH", "LIBCOSIM_C_LIBRARY"):
        env.pop(var, None)
    return env


def version():
    with open(os.path.join(SWIG_DIR, "conanfile.py"), encoding="utf-8") as f:
        for line in f:
            if line.strip().startswith("version ="):
                return line.split('"')[1]
    raise RuntimeError("no version in conanfile.py")


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--out", default=os.path.join(SWIG_DIR, "build-packages"))
    p.add_argument("--languages", nargs="+", choices=ALL, default=ALL)
    p.add_argument("--test", action="store_true", help="test the packages")
    p.add_argument("--test-data", default=os.path.join(SWIG_DIR, "..", "..", "tests", "data"),
                   help="libcosim's tests/data (default: the enclosing libcosim repository)")
    p.add_argument("--julia-url", help="where the Julia artifact tarballs will be downloaded "
                   "from (default: the GitHub release v<version>)")
    p.add_argument("--conan-arg", action="append", default=[],
                   help="extra argument for `conan install`, e.g. --conan-arg=-r=conancenter")
    args = p.parse_args()

    out = os.path.abspath(args.out)
    dist = os.path.join(out, "dist")
    langs = args.languages
    v = version()

    log("conan install")
    java_opt = ["-o", "&:java=True"] if "java" in langs else []  # JDK from Conan
    run(["conan", "install", SWIG_DIR, "-of", out, "--build=missing",
         "-s", "build_type=Release", *java_opt, *args.conan_arg])
    toolchain = (glob.glob(os.path.join(out, "build", "**", "conan_toolchain.cmake"), recursive=True) or [None])[0]
    if not toolchain:
        sys.exit("conan_toolchain.cmake not found")
    gen = os.path.dirname(toolchain)
    conan_env = os.path.join(gen, "conanbuild")

    cmake_langs = [l for l in langs if l != "python"]
    if cmake_langs:
        build = os.path.join(out, "cmake")
        log("configure")
        on = lambda lang: "ON" if lang in cmake_langs else "OFF"
        run(["cmake", "-S", SWIG_DIR, "-B", build,
             f"-DCMAKE_TOOLCHAIN_FILE={toolchain}", "-DCMAKE_BUILD_TYPE=Release",
             "-DLIBCOSIM_SWIG_INSTALL=ON", "-DLIBCOSIM_SWIG_PYTHON=OFF",
             f"-DLIBCOSIM_SWIG_C={'ON' if ('c' in langs or 'julia' in langs) else 'OFF'}",
             f"-DLIBCOSIM_SWIG_JULIA={on('julia')}", f"-DLIBCOSIM_SWIG_JAVA={on('java')}",
             f"-DLIBCOSIM_SWIG_CSHARP={on('csharp')}"], conan_env=conan_env)
        log("build")
        run(["cmake", "--build", build, "--config", "Release", "--parallel"], conan_env=conan_env)
        for lang in cmake_langs:
            log(f"package {lang}")
            shutil.rmtree(os.path.join(dist, lang), ignore_errors=True)
            # The C component installs to include/, lib/ (and bin/), the
            # others to a directory named after the language.
            prefix = os.path.join(dist, "c") if lang == "c" else dist
            run(["cmake", "--install", build, "--config", "Release",
                 "--prefix", prefix, "--component", lang], conan_env=conan_env)

    if "julia" in langs:
        log("package julia: artifact")
        jl = os.path.join(dist, "julia")
        url = args.julia_url or f"{RELEASES}/v{v}"
        run(["julia", "--startup-file=no", os.path.join(SWIG_DIR, "julia", "make_artifact.jl"),
             os.path.join(jl, "native"), jl, url],
            env=dict(os.environ, JULIA_DEPOT_PATH=os.path.join(out, "julia-depot")))
        shutil.rmtree(os.path.join(jl, "native"))
        # This platform's entry; a release concatenates the entries of all
        # platforms into Artifacts.toml.
        with open(os.path.join(jl, "Libcosim", "Artifacts.toml"), "w", encoding="utf-8") as f:
            for entry in sorted(glob.glob(os.path.join(jl, "Artifacts-*.toml"))):
                with open(entry, encoding="utf-8") as e:
                    f.write(e.read())

    if "python" in langs:
        log("package python")
        shutil.rmtree(os.path.join(dist, "python"), ignore_errors=True)
        run([sys.executable, "-m", "build", "--wheel", SWIG_DIR, "-o", os.path.join(dist, "python"),
             f"-Ccmake.define.CMAKE_TOOLCHAIN_FILE={toolchain}",
             # Visual Studio generates all configurations; the Conan
             # dependencies exist only for Release.
             "-Ccmake.define.CMAKE_CONFIGURATION_TYPES=Release",
             f"-Cbuild-dir={os.path.join(out, 'wheel-build')}"])  # the toolchain is enough

    if not args.test:
        return
    data = os.path.abspath(args.test_data)
    osp = os.path.join(data, "msmi", "OspSystemStructure.xml")
    identity = os.path.join(data, "fmi1", "identity.fmu")
    env = clean_env()
    tests = os.path.join(out, "tests")

    if "python" in langs:
        log("test python")
        venv = os.path.join(tests, "python-venv")
        shutil.rmtree(venv, ignore_errors=True)
        run([sys.executable, "-m", "venv", venv])
        py = os.path.join(venv, "Scripts" if WINDOWS else "bin", "python")
        wheel = glob.glob(os.path.join(dist, "python", "*.whl"))[0]
        run([py, "-m", "pip", "install", "-q", wheel], env=env)
        py_env = dict(env, LIBCOSIM_TEST_DATA=data)
        # A package with proxy-FMU support (the proxyfmu executable) is
        # tested with proxy FMUs too.
        with zipfile.ZipFile(wheel) as z:
            if any(os.path.basename(n) in ("proxyfmu", "proxyfmu.exe") for n in z.namelist()):
                py_env["LIBCOSIM_TEST_PROXYFMU"] = "1"
        run([py, "-m", "unittest", "discover", "-s", os.path.join(SWIG_DIR, "python", "tests"),
             "-p", "test_bindings.py"], env=py_env, cwd=tests)

    if "java" in langs:
        log("test java")
        jar = os.path.join(dist, "java", f"libcosim-{v}.jar")
        classes = os.path.join(tests, "java-classes")
        shutil.rmtree(classes, ignore_errors=True)
        # The JDK from Conan (its environment has no paths to libcosim)
        run(["javac", "-cp", jar, "-d", classes,
             os.path.join(SWIG_DIR, "java", "test", "libcosim", "SmokeTest.java")],
            env=env, conan_env=conan_env)
        run(["java", "-cp", os.pathsep.join([jar, classes]), "libcosim.SmokeTest", osp, identity],
            env=env, conan_env=conan_env)

    if "csharp" in langs:
        log("test csharp")
        consumer = os.path.join(SWIG_DIR, "csharp", "consumer", "Consumer.csproj")
        cs_out = os.path.join(tests, "csharp")
        shutil.rmtree(cs_out, ignore_errors=True)
        # A private package cache, so that a rebuilt package is never shadowed.
        cs_env = dict(env, NUGET_PACKAGES=os.path.join(cs_out, "packages"))
        run(["dotnet", "build", consumer, "--nologo", "-v", "quiet", "-c", "Release",
             f"-p:LibcosimVersion={v}", f"-p:BaseIntermediateOutputPath={cs_out}/obj/",
             "--source", os.path.join(dist, "csharp"), "-o", os.path.join(cs_out, "bin")], env=cs_env)
        run(["dotnet", os.path.join(cs_out, "bin", "Consumer.dll"), osp, identity], env=env)

    if "julia" in langs:
        log("test julia")
        # Install the package as a user would (Pkg downloads the artifact),
        # with the artifact URL pointing to the local tarball, in a fresh depot.
        jl_test = os.path.join(tests, "julia")
        shutil.rmtree(jl_test, ignore_errors=True)
        pkg = os.path.join(jl_test, "Libcosim")
        shutil.copytree(os.path.join(dist, "julia", "Libcosim"), pkg)
        tarballs = pathlib.Path(dist, "julia").as_uri()
        with open(os.path.join(pkg, "Artifacts.toml"), encoding="utf-8") as f:
            toml = re.sub(r'url = ".*/(libcosim_c-[^/"]+)"', lambda m: f'url = "{tarballs}/{m.group(1)}"', f.read())
        with open(os.path.join(pkg, "Artifacts.toml"), "w", encoding="utf-8") as f:
            f.write(toml)
        # The trailing separator keeps the default depots (registries).
        jl_env = dict(env, JULIA_DEPOT_PATH=os.path.join(jl_test, "depot") + os.pathsep)
        project = os.path.join(jl_test, "env")
        run(["julia", "--startup-file=no", f"--project={project}", "-e",
             f'import Pkg; Pkg.develop(path=raw"{pkg}")'], env=jl_env)
        run(["julia", "--startup-file=no", f"--project={project}",
             os.path.join(SWIG_DIR, "julia", "test", "runtests.jl"), osp, identity], env=jl_env)

    if "c" in langs:
        log("test c: run `conan create .` (builds and runs test_package)")

    log("all package tests passed")


if __name__ == "__main__":
    main()
