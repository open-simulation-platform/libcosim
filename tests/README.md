# Test FMUs

The C++ tests use the archives in `data/fmi1`, `data/fmi2`, and
`data/fmi3`. Normal test configuration and builds do not generate FMUs.
CMake supplies the C++ test paths.

The multi-platform fixture bundle contains Windows x86-64, Linux x86-64, and
macOS binaries (the FMI 1/2 binaries are universal, the FMI 3 binaries have
separate x86-64 and arm64 slices). Its `resources/README.md` files record
the exact source revisions and modifications; `sources/rebuild` contains
the generator recipes, sources, licenses, and applicable patches. These
archives use Reference-FMUs `v0.0.42`
(`75d16f87da2fba3c552c99cd683128a9591c1fa3`) and FMI Library `3.0.4`
(`4a4b21ec10a632b2768a604c2330c54204919644`). Existing prebuilt archives were
packaged with libcosim fixture sources at
`c09fafff0aea487b0e64c03ee429349c8fb6b2a2`; generated replacements record
their own revision.
The embedded rebuild instructions describe that historical generator's
`fmu1`/`fmu2`/`fmu3` layout; newly generated bundles use `fmi1`/`fmi2`/`fmi3`.
See [build-test-fmus](build-test-fmus/README.md) to generate and provision
them, including `StepProbe.fmu`, before running tests in a clean checkout.
On Windows, the embedded source trees can exceed the platform's path-length
limit when extracted into a deeply nested persistent FMU cache. Use a short
cache path when importing these archives.

## FMI 1

| FMU | Origin and test use |
| --- | --- |
| `EnumerationFixture.fmu` | Local Integer-backed Enumeration fixture; tests enumeration metadata and values. |
| `identity.fmu` | Legacy ViProMa demo FMU; exercises identity signals, FMI 1 importing, execution, proxy and mixed-version behavior. See [license](data/fmi1/viproma_demo-fmus_LICENSE). |

## FMI 2

| FMU | Origin and test use |
| --- | --- |
| `BouncingBall.fmu` | Unmodified Reference-FMUs model; bouncing-ball dynamics and events. |
| `Dahlquist.fmu` | Unmodified Reference-FMUs model; scalar dynamics, stepping and coupled Dahlquist configurations. |
| `Feedthrough.fmu` | Unmodified Reference-FMUs model; typed input/output and cross-version tests. |
| `Fmi2Derivatives.fmu` | Local directional-derivative and status fixture; exercises FMI 2 derivative handling. |
| `MixedTypes.fmu` | Local Real, Integer, Boolean and Enumeration fixture; exercises mixed-type connections. |
| `Resource.fmu` | Unmodified Reference-FMUs model; exercises FMU resources. |
| `Stair.fmu` | Unmodified Reference-FMUs model; discrete stair behavior. |
| `VanDerPol.fmu` | Unmodified Reference-FMUs oscillator; exercises integration and stepping. |
| `CraneController.fmu`, `KnuckleBoomCrane.fmu` | Legacy OSP crane models used in OSP system-structure configurations. |
| `StateInitExample.fmu` | Legacy initial-state example used in OSP configurations. |
| `vector.fmu` | Legacy OSP cpp-fmus model; tests typed values, orchestration, proxy and mixed-version systems. See [license](data/fmi2/osp_cpp-fmus_LICENSE). |
| `quarter_truck/Chassis.fmu`, `quarter_truck/Wheel.fmu` | Legacy OSP quarter-truck models used for ECCO power-bond and logging tests. See [license](data/fmi2/quarter_truck/LICENSE). |

## FMI 3

| FMU | Origin, modification and test use |
| --- | --- |
| `BouncingBall.fmu` | Unmodified Reference-FMUs model; dynamics and event handling. |
| `Clocks.fmu` | Unmodified Scheduled Execution reference model; tests rejection of the unsupported interface. |
| `Dahlquist.fmu` | Unmodified Reference-FMUs model; stepping and coupled OSP systems. |
| `Feedthrough.fmu` | Unmodified Reference-FMUs model; exact types, values, callbacks and execution. |
| `Resource.fmu` | Unmodified Reference-FMUs model; resource handling. |
| `Roberts.fmu`, `VanDerPol.fmu` | Unmodified Reference-FMUs models; dynamics and simulation. |
| `Stair.fmu` | Unmodified Reference-FMUs model; discrete events and early return. |
| `StateSpace.fmu` | Unmodified Reference-FMUs model; state-space behavior and FMI capabilities. |
| `ArrayFeedthrough.fmu`, `FixedArrayStateSpace.fmu` | Feedthrough and StateSpace patched using `reference_fmus.patch`; test whole-array shapes, values, state and connections. |
| `ClockedCoSimulation.fmu` | Custom Co-Simulation implementation of Clocks with transformed metadata; tests clock scheduling and clock-associated values. |
| `DiscreteStair.fmu` | Stair with early-return capability removed from its metadata; tests event-mode behavior without early return. |
| `Enumeration.fmu` | Local scalar and array Enumeration fixture; tests metadata, values and connections. |
| `Fmi3Derivatives.fmu` | Local directional and adjoint derivative fixture; tests derivative values and error/status paths. |
| `IntermediateFeedthrough.fmu`, `IntermediateStateSpace.fmu` | Reference models with intermediate-update metadata added; test intermediate callbacks. |
| `OutputDerivativesBouncingBall.fmu` | BouncingBall with output-derivative capability added to its metadata; tests output derivative access. |
| `PowerBond.fmu` | Custom model using `ecco_power_bond.patch` on the Reference-FMUs framework; tests ECCO power bonds. |
| `StatusFault.fmu` | Reference framework with renamed functions and custom status wrappers; tests warnings, errors and fault handling. |
| `StepProbe.fmu` | Modified BouncingBall with a controlled `DoStep` wrapper; tests intermediate early return and event timing. |
| `StrictClock.fmu` | Local strict clock-access fixture; tests clock scheduling and permitted data access. |
| `nonconverging/Stair.fmu` | Stair with a nonconverging event-update wrapper; tests the event-iteration limit. |
| `unscheduled/Stair.fmu` | Stair with unscheduled-event wrappers; tests event scheduling behavior. |

The OSP XML files in `data/fmi3/system_structure` are copied to the build
tree with the prebuilt FMUs they reference so relative paths resolve. The
legacy FMUs above predate the generated-fixture workflow and are not rebuilt
by it.
