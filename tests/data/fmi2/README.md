# FMI 2.0 test FMUs

| Name                   | Origin                  | License                                  |
|------------------------|-------------------------|------------------------------------------|
| `CraneController.fmu`  | OSP                     | [MPL 2.0](../../../LICENSE)              |
| `KnuckleBoomCrane.fmu` | OSP                     | [MPL 2.0](../../../LICENSE)              |
| `vector.fmu`           | [OSP cpp-fmus]          | [MIT](./osp_cpp-fmus_LICENSE)            |

`Dahlquist.fmu` is generated from the pinned
[Reference-FMUs] release during CMake configuration and build. FMI 2 tests
receive its path through `REFERENCE_FMU_V2_DIR`.


[OSP cpp-fmus]: https://github.com/open-simulation-platform/cpp-fmus
[Reference-FMUs]: https://github.com/open-simulation-platform/Reference-FMUs
