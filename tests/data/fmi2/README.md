# FMI 2.0 test FMUs

| Name                   | Origin                  | License                                  |
|------------------------|-------------------------|------------------------------------------|
| `CraneController.fmu`  | OSP                     | [MPL 2.0](../../../LICENSE)              |
| `KnuckleBoomCrane.fmu` | OSP                     | [MPL 2.0](../../../LICENSE)              |
| `vector.fmu`           | [OSP cpp-fmus]          | [MIT](./osp_cpp-fmus_LICENSE)            |

The prebuilt `BouncingBall`, `Dahlquist`, `Feedthrough`, `Resource`, `Stair`
and `VanDerPol` archives are based on [Reference-FMUs]. `Fmi2Derivatives` and
`MixedTypes` are local fixtures. Their sources and provenance are embedded in
each archive. See the [test FMU overview](../../README.md) for the complete
inventory, including `StateInitExample` and the quarter-truck FMUs.


[OSP cpp-fmus]: https://github.com/open-simulation-platform/cpp-fmus
[Reference-FMUs]: https://github.com/open-simulation-platform/Reference-FMUs
