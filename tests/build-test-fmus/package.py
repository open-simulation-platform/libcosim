"""Stage platform builds and merge them into source-bearing test FMUs."""

import argparse
import difflib
from pathlib import Path
import shutil
import subprocess
import tempfile
import xml.etree.ElementTree as ET
import zipfile


EXPECTED = {
    "fmi1/EnumerationFixture.fmu",
    *(f"fmi2/{name}.fmu" for name in (
        "BouncingBall", "Dahlquist", "Feedthrough", "Fmi2Derivatives",
        "MixedTypes", "Resource", "Stair", "VanDerPol",
    )),
    *(f"fmi3/{name}.fmu" for name in (
        "ArrayFeedthrough", "BouncingBall", "ClockedCoSimulation", "Clocks",
        "Dahlquist", "DiscreteStair", "Enumeration", "Feedthrough",
        "FixedArrayStateSpace", "Fmi3Derivatives", "IntermediateFeedthrough",
        "IntermediateStateSpace", "OutputDerivativesBouncingBall", "PowerBond",
        "Resource", "Roberts", "Stair", "StateSpace", "StatusFault", "StepProbe",
        "StrictClock", "VanDerPol",
    )),
    "fmi3/nonconverging/Stair.fmu",
    "fmi3/unscheduled/Stair.fmu",
}

PLATFORMS = {
    "windows": ("win64", "x86_64-windows", ".dll"),
    "linux": ("linux64", "x86_64-linux", ".so"),
    "macos-intel": ("darwin64", "x86_64-darwin", ".dylib"),
    "macos-arm": ("aarch64-darwin", "aarch64-darwin", ".dylib"),
}

METADATA_DERIVATIVES = {
    "fmi3/ClockedCoSimulation.fmu": "Clocks",
    "fmi3/DiscreteStair.fmu": "Stair",
    "fmi3/IntermediateFeedthrough.fmu": "Feedthrough",
    "fmi3/IntermediateStateSpace.fmu": "StateSpace",
    "fmi3/OutputDerivativesBouncingBall.fmu": "BouncingBall",
}


def describe(data):
    root = ET.fromstring(data)
    version = root.attrib["fmiVersion"].split(".")[0]
    if version == "1":
        identifier = root.attrib["modelIdentifier"]
    else:
        interface = root.find("CoSimulation")
        if interface is None:
            interface = root.find("ScheduledExecution")
        if interface is None:
            raise ValueError("FMU has no Co-Simulation or Scheduled Execution interface")
        identifier = interface.attrib["modelIdentifier"]
    if version not in ("1", "2", "3") or not identifier:
        raise ValueError("Invalid FMI version or model identifier")
    return version, identifier


def stage(build, output):
    archives = [
        *sorted((build / "reference-fmus-build" / "fmus").glob("*.fmu")),
        *sorted((build / "reference-fmus-v2-build" / "fmus").glob("*.fmu")),
        *sorted((build / "fmi3-array-fixtures").glob("*.fmu")),
        build / "fmi1-enumeration" / "EnumerationFixture.fmu",
        build / "fmi3-nonconverging" / "Stair.fmu",
        build / "fmi3-unscheduled" / "Stair.fmu",
        build / "fmi3-step-probe" / "StepProbe.fmu",
    ]
    found = set()
    for archive in archives:
        with zipfile.ZipFile(archive) as source:
            version, _ = describe(source.read("modelDescription.xml"))
        subdir = ""
        if archive.parent.name == "fmi3-nonconverging":
            subdir = "nonconverging/"
        elif archive.parent.name == "fmi3-unscheduled":
            subdir = "unscheduled/"
        name = f"fmi{version}/{subdir}{archive.name}"
        if name in found:
            raise ValueError(f"Duplicate FMU: {name}")
        found.add(name)
        destination = output / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(archive, destination)
    if found != EXPECTED:
        raise ValueError(f"FMU inventory mismatch: missing={EXPECTED - found}, extra={found - EXPECTED}")
    staged = input_files(output)
    if staged != EXPECTED:
        raise ValueError(f"Staged FMU inventory mismatch: missing={EXPECTED - staged}, extra={staged - EXPECTED}")


def input_files(folder):
    return {p.relative_to(folder).as_posix() for p in folder.rglob("*.fmu")}


def bundle(folder, output):
    actual = input_files(folder)
    if actual != EXPECTED:
        raise ValueError(f"FMU inventory mismatch: missing={EXPECTED - actual}, extra={actual - EXPECTED}")
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=output.parent, suffix=".zip", delete=False) as temporary:
        temporary_path = Path(temporary.name)
    try:
        with zipfile.ZipFile(temporary_path, "w", compression=zipfile.ZIP_STORED) as archive:
            for name in sorted(EXPECTED):
                archive.write(folder / name, name)
        temporary_path.replace(output)
    finally:
        temporary_path.unlink(missing_ok=True)


def source_entries(repo, reference, fmi_library):
    entries = {}

    def add_tree(source, destination, excluded=()):
        if not source.is_dir():
            raise ValueError(f"Missing source directory: {source}")
        count = 0
        for file in sorted(source.rglob("*")):
            if not file.is_file() or any(part in excluded for part in file.relative_to(source).parts):
                continue
            entries[f"sources/rebuild/{destination}/{file.relative_to(source).as_posix()}"] = file.read_bytes()
            count += 1
        if not count:
            raise ValueError(f"Empty source directory: {source}")

    add_tree(reference, "reference-fmus", {".git", "build", "__pycache__"})
    add_tree(repo / "tests" / "build-test-fmus", "tests/build-test-fmus", {"__pycache__"})
    add_tree(fmi_library / "ThirdParty" / "FMI" / "default" / "FMI1", "fmi-library/FMI1")
    add_tree(fmi_library / "ThirdParty" / "FMI" / "default" / "FMI2", "fmi-library/FMI2")
    for source, destination in (
        (repo / "LICENSE", "libcosim-LICENSE"),
        (fmi_library / "LICENSE.md", "fmi-library-LICENSE"),
        (fmi_library / "FMILIB_License.txt", "fmi-library-notices.txt"),
    ):
        entries[f"sources/rebuild/{destination}"] = source.read_bytes()
    return entries


def normalize_xml(data):
    return data.replace(b"\r\n", b"\n").replace(b"\r", b"\n")


def metadata_diff(name, xml, reference):
    model = METADATA_DERIVATIVES.get(name)
    if model is None:
        return None
    upstream = normalize_xml((reference / model / "FMI3.xml").read_bytes())
    modified = normalize_xml(xml)
    if upstream == modified:
        raise ValueError(f"{name}: expected metadata changes are missing")
    return "".join(difflib.unified_diff(
        upstream.decode("utf-8").splitlines(keepends=True),
        modified.decode("utf-8").splitlines(keepends=True),
        fromfile=f"Reference-FMUs/{model}/FMI3.xml",
        tofile=f"{name}/modelDescription.xml",
    )).encode("utf-8")


def provenance(name, revision, fmi_revision, libcosim_revision):
    modifications = {
        "EnumerationFixture": "Local FMI 1 fixture with Integer-backed Enumeration values.",
        "Fmi2Derivatives": "Local FMI 2 directional-derivative and status fixture.",
        "MixedTypes": "Local FMI 2 Real, Integer, Boolean and Enumeration interoperability fixture.",
        "ArrayFeedthrough": "Feedthrough patched with reference_fmus.patch.",
        "FixedArrayStateSpace": "StateSpace patched with reference_fmus.patch.",
        "PowerBond": "Custom PowerBond model and ecco_power_bond.patch applied to the framework.",
        "ClockedCoSimulation": "Custom Co-Simulation implementation of Clocks and transformed metadata.",
        "DiscreteStair": "Stair with early-return capability removed from its metadata.",
        "Enumeration": "Local FMI 3 scalar and array Enumeration fixture.",
        "Fmi3Derivatives": "Local FMI 3 directional and adjoint derivative fixture.",
        "IntermediateFeedthrough": "Feedthrough with intermediate-update metadata added.",
        "IntermediateStateSpace": "StateSpace with intermediate-update metadata added.",
        "OutputDerivativesBouncingBall": "BouncingBall with output-derivative capability added.",
        "StatusFault": "Custom status wrappers and generated, renamed framework functions.",
        "StepProbe": "BouncingBall with a custom DoStep wrapper for early-return and event-timing tests.",
        "StrictClock": "Local FMI 3 strict clock access fixture.",
    }
    stem = Path(name).stem
    change = modifications.get(stem, "Unmodified model from the Reference-FMUs release.")
    if "/nonconverging/" in name:
        change = "Stair with a custom nonconverging event-update wrapper."
    elif "/unscheduled/" in name:
        change = "Stair with custom unscheduled-event wrappers."
    return f"""# Test FMU provenance and rebuilding

Archive: `{name}`

Reference-FMUs: release `v0.0.42`, commit `{revision}`.
FMI Library headers: release `3.0.4`, commit `{fmi_revision}`.
libcosim fixture source commit: `{libcosim_revision}`.
libcosim fixture sources and CMake recipes are included under `sources/rebuild`.
Modification: {change}

From the extracted FMU directory, configure and build using CMake, a C/C++
compiler and GNU patch. These commands use a POSIX shell (for example, Git
Bash on Windows):

```
cmake -S sources/rebuild/tests/build-test-fmus -B build -DCMAKE_BUILD_TYPE=Release \\
  -DREFERENCE_FMUS_SOURCE_DIR="$PWD/sources/rebuild/reference-fmus" \\
  -DFMILibrary_INCLUDE_DIRS="$PWD/sources/rebuild/fmi-library"
cmake --build build --config Release --target build-test-fmus --parallel
```

The rebuilt platform archive is staged at `build/staged-test-fmus/{name}` and
included in `build/generated-test-fmus.zip`. This builds all fixture FMUs,
not just this archive. Recreating the combined multi-platform
archive additionally requires builds from all four platforms and the merge
command in the included packaging script, run on macOS with `lipo`.

The generated FMU is functionally equivalent; byte-identical output is not
guaranteed across compilers, SDKs, paths, timestamps or archive tools.
The source bundle contains all recipes, including the metadata transformations
and patches. Metadata-only transformations are also provided as diffs under
`resources/patches`. The old upstream-only `sources` content is not retained for the
modified ClockedCoSimulation binary.
"""


def merge(platform_root, output, repo, reference, fmi_library, revision, fmi_revision, libcosim_revision):
    for platform in PLATFORMS:
        actual = input_files(platform_root / platform)
        if actual != EXPECTED:
            raise ValueError(f"{platform} inventory mismatch: missing={EXPECTED - actual}, extra={actual - EXPECTED}")
    sources = source_entries(repo, reference, fmi_library)
    for name in sorted(EXPECTED):
        payload = {}
        metadata = None
        version = identifier = None
        mac_binaries = {}
        for platform, (legacy_platform, fmi3_platform, suffix) in PLATFORMS.items():
            with zipfile.ZipFile(platform_root / platform / name) as archive:
                xml = normalize_xml(archive.read("modelDescription.xml"))
                if metadata is None:
                    metadata = xml
                    version, identifier = describe(xml)
                    for entry in archive.infolist():
                        if entry.is_dir() or entry.filename.startswith(("binaries/", "resources/build-info/")):
                            continue
                        if name.endswith("/ClockedCoSimulation.fmu") and entry.filename.startswith("sources/"):
                            continue
                        payload[entry.filename] = archive.read(entry)
                elif metadata != xml:
                    raise ValueError(f"{name}: modelDescription.xml differs on {platform}")
                directory = fmi3_platform if version == "3" else legacy_platform
                binary = f"binaries/{directory}/{identifier}{suffix}"
                data = archive.read(binary)
                if platform.startswith("macos-") and version != "3":
                    mac_binaries[platform] = data
                else:
                    payload[binary] = data
        if mac_binaries:
            with tempfile.TemporaryDirectory() as temp:
                temp = Path(temp)
                intel, arm, universal = temp / "intel", temp / "arm", temp / "universal"
                intel.write_bytes(mac_binaries["macos-intel"])
                arm.write_bytes(mac_binaries["macos-arm"])
                subprocess.run(["lipo", "-create", str(intel), str(arm), "-output", str(universal)], check=True)
                subprocess.run(["lipo", str(universal), "-verify_arch", "x86_64", "arm64"], check=True)
                payload[f"binaries/darwin64/{identifier}.dylib"] = universal.read_bytes()
        payload.update(sources)
        if Path(name).stem in ("ArrayFeedthrough", "FixedArrayStateSpace"):
            patch = repo / "tests" / "build-test-fmus" / "fmi3_array_fixture" / "reference_fmus.patch"
            payload["resources/patches/reference_fmus.patch"] = patch.read_bytes()
        if Path(name).stem == "PowerBond":
            patch = repo / "tests" / "build-test-fmus" / "fmi3_ecco_power_bond_fixture" / "ecco_power_bond.patch"
            payload["resources/patches/ecco_power_bond.patch"] = patch.read_bytes()
        diff = metadata_diff(name, metadata, reference)
        if diff is not None:
            payload["resources/patches/modelDescription.patch"] = diff
        payload["resources/README.md"] = provenance(name, revision, fmi_revision, libcosim_revision).encode()
        destination = output / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED) as archive:
            for entry, content in sorted(payload.items()):
                archive.writestr(entry, content)


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    stager = commands.add_parser("stage")
    stager.add_argument("build", type=Path)
    stager.add_argument("output", type=Path)
    bundler = commands.add_parser("bundle")
    bundler.add_argument("folder", type=Path)
    bundler.add_argument("output", type=Path)
    merger = commands.add_parser("merge")
    for arg in ("platform_root", "output", "repo", "reference", "fmi_library"):
        merger.add_argument(arg, type=Path)
    merger.add_argument("revision")
    merger.add_argument("fmi_revision")
    merger.add_argument("libcosim_revision")
    args = parser.parse_args()
    if args.command == "stage":
        stage(args.build, args.output)
    elif args.command == "bundle":
        bundle(args.folder, args.output)
    else:
        merge(args.platform_root, args.output, args.repo, args.reference, args.fmi_library, args.revision, args.fmi_revision, args.libcosim_revision)


if __name__ == "__main__":
    main()
