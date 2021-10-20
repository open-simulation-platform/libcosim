# Change log
All notable changes to libcosim will be documented in this file. This includes new features, bug fixes and breaking changes. For a more detailed list of all changes, click the header links for each version. 

This project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html)

### [v0.8.2] – 2021-10-xx
##### Fixed
* Update to fmilibrary 2.3 ([PR#659](https://github.com/open-simulation-platform/libcosim/pull/659))
* Update to proxyfmu 0.2.4 ([PR#663](https://github.com/open-simulation-platform/libcosim/pull/663))

### [v0.8.1] – 2021-09-15
##### Fixed
* Using proxyfmu v0.2.3 that fixes an assumtion that proxyfmu executable being located in working directory ([PR#657](https://github.com/open-simulation-platform/libcosim/pull/657), [proxyfmu#40](https://github.com/open-simulation-platform/proxy-fmu/issues/40))
* Stopped-flag not reset when end-time exceeded ([PR#656](https://github.com/open-simulation-platform/libcosim/pull/656))

### [v0.8.0] – 2021-09-03

##### Changed
* Replacing JVM based fmu-proxy with [proxy-fmu](https://github.com/open-simulation-platform/proxy-fmu) ([PR#633](https://github.com/open-simulation-platform/libcosim/pull/633))
* Replacing `boost::filesystem` with `std::filesystem` ([PR#637](https://github.com/open-simulation-platform/libcosim/pull/637))
 
##### Fixed
* Memory leaks in observers ([PR#647](https://github.com/open-simulation-platform/libcosim/pull/647))

### [v0.7.1] – 2021-06-03

##### Added
* improving documentation ([PR#617](https://github.com/open-simulation-platform/libcosim/pull/617))
 
##### Fixed
* Compile using VS2019([PR#638](https://github.com/open-simulation-platform/libcosim/pull/638))
* Allow empty sequence of initial values in OspSystemStructure ([PR#613](https://github.com/open-simulation-platform/libcosim/pull/613))
* Model description only exposed through execution ([PR#603](https://github.com/open-simulation-platform/libcosim/pull/603))
* Validate initial variable values from OspSystemStructure ([PR#604](https://github.com/open-simulation-platform/libcosim/pull/604))
* Real time timer improvements ([PR#605](https://github.com/open-simulation-platform/libcosim/pull/605))
* Link dependencies dynamically ([PR#596](https://github.com/open-simulation-platform/libcosim/pull/596))
* updating dependencies and resolving conflicts ([PR#610](https://github.com/open-simulation-platform/libcosim/pull/610), 
[PR#632](https://github.com/open-simulation-platform/libcosim/pull/632), 
[PR#641](https://github.com/open-simulation-platform/libcosim/pull/641))

### [v0.7.0] – 2020-05-29

##### Changed
* Renamed library from csecore to libcosim ([PR#571](https://github.com/open-simulation-platform/libcosim/pull/571))
* C API moved to separate repository [libcosimc](https://github.com/open-simulation-platform/libcosimc) ([PR#571](https://github.com/open-simulation-platform/libcosim/pull/571))
* Split `model.hpp` into `model_description.hpp` and `time.hpp` ([PR#587](https://github.com/open-simulation-platform/libcosim/pull/587))

##### Added
* Provide library version in API ([PR#586](https://github.com/open-simulation-platform/libcosim/pull/586))
 
##### Fixed
* Incorrect parsing of VectorSum function dimension ([PR#580](https://github.com/open-simulation-platform/libcosim/pull/580))
* Arbitrary order for child elements in OspSystemStructure.xml `<connections>`([PR#575](https://github.com/open-simulation-platform/libcosim/pull/575))

### [v0.6.0] – 2020-04-27
##### Changed
* MSMI configuration (OspSystemStructure.xml and OspModelDescription.xml) updated 
     * plugs, sockets and bonds replaced with variable groups 
     ([PR#496](https://github.com/open-simulation-platform/cse-core/pull/496),
      [PR#530](https://github.com/open-simulation-platform/cse-core/pull/530), 
      [PR#537](https://github.com/open-simulation-platform/cse-core/pull/537))
     * configuration for `Function` to `FMU` connections ([PR#517](https://github.com/open-simulation-platform/cse-core/pull/517))
     * Include FMI 2.0 unit xsd in OspModelDescription.xml ([#523](https://github.com/open-simulation-platform/cse-core/issues/523))
 * multi-variable connections replaced with functions ([PR#518](https://github.com/open-simulation-platform/cse-core/pull/518))
 * algorithm configuration moved to DefaultExperiment in .ssd ([PR#447](https://github.com/open-simulation-platform/cse-core/pull/447)) 
 * OspModelDescription file location at FMU path or configuration path ([#462](https://github.com/open-simulation-platform/cse-core/issues/462))
 
##### Added
* `Functions` for calculations between time steps ([PR#517](https://github.com/open-simulation-platform/cse-core/pull/517))
* Support setting initial values through C API ([#328](https://github.com/open-simulation-platform/cse-core/issues/328))
* Increased SSP support:
    * support .ssp files ([#491](https://github.com/open-simulation-platform/cse-core/issues/491))
    * support individual model step-size configuration in SSP ([#435](https://github.com/open-simulation-platform/cse-core/issues/435))
    * support external ssv files (parameter sets) ([PR#489](https://github.com/open-simulation-platform/cse-core/pull/489))
    * support multiple ParameterSets in SSP ([#483](https://github.com/open-simulation-platform/cse-core/issues/483))
    * support multiple .ssd in an .ssp ([#483](https://github.com/open-simulation-platform/cse-core/issues/483))
    * support linear transformations ([#451](https://github.com/open-simulation-platform/cse-core/issues/451))
* Support on/off toggling of file observer ([#508](https://github.com/open-simulation-platform/cse-core/issues/508))
* Add functionality for offline model building ([#114](https://github.com/open-simulation-platform/cse-core/issues/114))
* Support stateful and/or time-dependent manipulators ([#203](https://github.com/open-simulation-platform/cse-core/issues/203))
* Persistent FMU cache ([PR#388](https://github.com/open-simulation-platform/cse-core/pull/388))
* Update fmu-proxy client to match upstream v0.6.1 ([#434](https://github.com/open-simulation-platform/cse-core/issues/434))
* Scenarios can be written in YAML ([#271](https://github.com/open-simulation-platform/cse-core/issues/271))
* Allow CSV files produced by `file_observer` to be generated without a timestamp ([PR#555](https://github.com/open-simulation-platform/cse-core/pull/555))
##### Fixed
* First line in output files is just zeroes ([#486](https://github.com/open-simulation-platform/cse-core/issues/486)) 
* `ssp_parser` does not handle connections correctly ([#479](https://github.com/open-simulation-platform/cse-core/issues/479))
* Typo causes SSP parser to ignore "Connector" elements ([#274](https://github.com/open-simulation-platform/cse-core/issues/274))


### [v0.5.1] – 2019-11-01
##### Changed
* Move `visitor` to utility header ([PR#425](https://github.com/open-simulation-platform/cse-core/pull/425))

##### Fixed
* Warning about string truncation in newer GCCs ([#418](https://github.com/open-simulation-platform/cse-core/issues/418))
* Simulation errors are not propagated when using `cse_execution_start()` ([#415](https://github.com/open-simulation-platform/cse-core/issues/415))
* Sum connections not parsed properly from `OspSystemStructure.xml` ([#429](https://github.com/open-simulation-platform/cse-core/issues/429))
* True/false boolean initial values not supported in `OspSystemStructure.xml` ([#420](https://github.com/open-simulation-platform/cse-core/issues/420))
* Fails to load FMU with empty folders in the `resources` directory ([#440](https://github.com/open-simulation-platform/cse-core/issues/440))
* Conflicting Conan dependencies ([#450](https://github.com/open-simulation-platform/cse-core/issues/450)) 
* Scenario does not support string values ([#354](https://github.com/open-simulation-platform/cse-core/issues/354))

### [v0.5.0] – 2019-10-03 
##### Added
* Multi-variable connections ([PR#295](https://github.com/open-simulation-platform/cse-core/pull/295))
* Support for MSMI extension to FMUs `<FMU-name>_OspModelDescription.xml`, that enables mapping of FMU variables into plugs, sockets and bonds ([PR#333](https://github.com/open-simulation-platform/cse-core/pull/333))
* New CSE configuration format `OspSystemStructure.xml` ([PR#333](https://github.com/open-simulation-platform/cse-core/pull/333)), that enables configuration of: 
    * Individual model step size
    * Plug/socket and bond connections
    * Multi-variable connections. Only sum connection included.
* Can advance an execution to a specific point in time with `cse_execution_simulate_until` in C-API ([PR#325](https://github.com/open-simulation-platform/cse-core/pull/325))
* Can override configured step size and start time for fixed-step algorithm in `cse_ssp_fixed_step_execution_create` and `cse_ssp_execution_create` in C-API ([PR#373](https://github.com/open-simulation-platform/cse-core/pull/373))

##### Changed
* Must provide instance name when creating local slaves with `cse_local_slave_create`. (Fixing [#381](https://github.com/open-simulation-platform/cse-core/issues/381), [PR#387](https://github.com/open-simulation-platform/cse-core/pull/387))
* `cse_variable_index` renamed to `cse_value_reference` [PR#339](https://github.com/open-simulation-platform/cse-core/pull/339)
   
 ##### Fixed
 * `cse_local_slave_create` is broken when adding more than one instance of the same FMU ([#381](https://github.com/open-simulation-platform/cse-core/issues/381))
 * Memory error when moving a cosim::uri ([#361](https://github.com/open-simulation-platform/cse-core/issues/361))
 * Cryptic error message with ill-defined or missing osp:FixedStepMaster in SSP ([#338](https://github.com/open-simulation-platform/cse-core/issues/338))
 * Invalid connections in SSP are not properly handled ([#336](https://github.com/open-simulation-platform/cse-core/issues/336))
 * Cannot use "." as file_observer output directory ([#310](https://github.com/open-simulation-platform/cse-core/issues/310))

### [v0.4.0] – 2019-08-19 
##### Added
* Initial variable values configurable from SSP ([PR#291](https://github.com/open-simulation-platform/cse-core/pull/291))
* Allow boolean to be set from scenario ([PR#292](https://github.com/open-simulation-platform/cse-core/pull/292))
* Providing overview of modified variables ([PR#296](https://github.com/open-simulation-platform/cse-core/pull/296))
* Give client code more control over logging ([PR#314](https://github.com/open-simulation-platform/cse-core/pull/314))
   
##### Fixed
* Cannot run JavaFMI generated FMUs ([PR#268](https://github.com/open-simulation-platform/cse-core/pull/268))
* Prevent crash when a variable is reconnected ([PR#285](https://github.com/open-simulation-platform/cse-core/pull/285))

### [v0.3.0] – 2019-06-26 
##### Added
* fmu-proxy integration ([PR#162](https://github.com/open-simulation-platform/cse-core/pull/162), [PR#239](https://github.com/open-simulation-platform/cse-core/pull/239))
* introducing`orchestration` interface for classes that resolve model URIs of one or more specific URI schemes ([PR#233](https://github.com/open-simulation-platform/cse-core/pull/233)) 
* logging configuration ([PR#247](https://github.com/open-simulation-platform/cse-core/pull/247))
* observers can observe string and boolean variables ([PR#257](https://github.com/open-simulation-platform/cse-core/pull/257))
* can set arbitraty real time factor ([PR#261](https://github.com/open-simulation-platform/cse-core/pull/261))
* improved error reporting
   
##### Fixed
* Prevent crash when FMU GUIDs include curly brackets ([#244](https://github.com/open-simulation-platform/cse-core/issues/244))
* Prevent crash when FMU includes enumerated variables ([#246](https://github.com/open-simulation-platform/cse-core/issues/246))

### [v0.2.0] – 2019-03-21
##### Added
* Scenario: Automatically modify any variables (except with causality local) on specified simulation time using scenario files
* Multi threaded co-simulation
* Introducing `manipulator` interface for manipulating variables
* `fixed_step_algorithm` supports executing FMUs with different time steps

##### Fixed
* Preventing observers from consuming increasing amount of memory

### v0.1.0 – 2019-01-04
First OSP JIP partner release

[v0.2.0]: https://github.com/open-simulation-platform/cse-core/compare/v0.1.0...v0.2.0
[v0.3.0]: https://github.com/open-simulation-platform/cse-core/compare/v0.2.0...v0.3.0
[v0.4.0]: https://github.com/open-simulation-platform/cse-core/compare/v0.3.0...v0.4.0
[v0.5.0]: https://github.com/open-simulation-platform/cse-core/compare/v0.4.0...v0.5.0
[v0.5.1]: https://github.com/open-simulation-platform/cse-core/compare/v0.5.0...v0.5.1
[v0.6.0]: https://github.com/open-simulation-platform/cse-core/compare/v0.5.1...v0.6.0
[v0.7.0]: https://github.com/open-simulation-platform/cse-core/compare/v0.6.0...v0.7.0
[v0.7.1]: https://github.com/open-simulation-platform/cse-core/compare/v0.7.0...v0.7.1
[v0.8.0]: https://github.com/open-simulation-platform/cse-core/compare/v0.7.1...v0.8.0
[v0.8.1]: https://github.com/open-simulation-platform/cse-core/compare/v0.8.0...v0.8.1
[v0.8.2]: https://github.com/open-simulation-platform/cse-core/compare/v0.8.1...v0.8.2
