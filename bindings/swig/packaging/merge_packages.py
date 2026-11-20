"""
Merges the per-platform packages made by build_packages.py into the
multi-platform release packages.

    python packaging/merge_packages.py <out dir> <dist dir> [<dist dir> ...]

Each <dist dir> is the build-packages/dist directory of one platform. Writes
to <out dir>:
    libcosim-<v>.jar            classes + libcosim/native/<platform>/ of all platforms
    Libcosim.<v>.nupkg          lib/ + runtimes/<rid>/native/ of all platforms
    libcosim_c-<triplet>.tar.gz Julia artifacts, copied
    Artifacts.toml              the Julia package's Artifacts.toml, all platforms
    Libcosim.jl.zip             the Julia package source, with that Artifacts.toml

The JAR and the NuGet package are zip files whose per-platform parts live
under distinct paths (native/<platform>/, runtimes/<rid>/); everything else
is the same on all platforms (taken from the first). The Python wheels and
the C (Conan) packages are per platform anyway.
"""
import glob
import os
import shutil
import sys
import zipfile


def merge_zips(sources, target, platform_prefix):
    """Writes target with all entries of sources[0], plus the entries under
    platform_prefix of the other sources."""
    seen = set()
    with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as out:
        for i, src in enumerate(sources):
            with zipfile.ZipFile(src) as z:
                for info in z.infolist():
                    if info.filename in seen:
                        continue
                    if i > 0 and not info.filename.startswith(platform_prefix):
                        continue
                    seen.add(info.filename)
                    out.writestr(info, z.read(info.filename))
    platforms = sorted({n[len(platform_prefix):].split("/")[0] for n in seen
                        if n.startswith(platform_prefix) and n != platform_prefix})
    print(f"{os.path.basename(target)}: {', '.join(platforms)}")


def one(pattern):
    found = glob.glob(pattern)
    if len(found) != 1:
        sys.exit(f"expected one file matching {pattern}, found {found}")
    return found[0]


def main(out, dists):
    os.makedirs(out, exist_ok=True)

    jars = [one(os.path.join(d, "java", "libcosim-*.jar")) for d in dists]
    merge_zips(jars, os.path.join(out, os.path.basename(jars[0])), "libcosim/native/")

    nupkgs = [one(os.path.join(d, "csharp", "Libcosim.*.nupkg")) for d in dists]
    merge_zips(nupkgs, os.path.join(out, os.path.basename(nupkgs[0])), "runtimes/")

    entries = []
    for d in dists:
        for tarball in glob.glob(os.path.join(d, "julia", "libcosim_c-*.tar.gz")):
            shutil.copy2(tarball, out)
        for toml in glob.glob(os.path.join(d, "julia", "Artifacts-*.toml")):
            with open(toml, encoding="utf-8") as f:
                entries.append(f.read())
    with open(os.path.join(out, "Artifacts.toml"), "w", encoding="utf-8") as f:
        f.write("\n".join(entries))
    print(f"Artifacts.toml: {len(entries)} platform(s)")

    # The complete Julia package (with the generated ccall layer, the same on
    # all platforms) and the Artifacts.toml of all platforms: what goes into
    # the git repository registered in Julia's General registry.
    pkg = os.path.join(out, "Libcosim.jl")
    shutil.rmtree(pkg, ignore_errors=True)
    shutil.copytree(os.path.join(dists[0], "julia", "Libcosim"), pkg)
    shutil.copy2(os.path.join(out, "Artifacts.toml"), pkg)
    shutil.make_archive(pkg, "zip", pkg)
    shutil.rmtree(pkg)
    print("Libcosim.jl.zip: Julia package source")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2:])
