# Change log
All notable changes to cse-core will be documented in this file. This includes new features, bug fixes and breaking changes. For a more detailed list of all changes, click the header links for each version. 

This project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html)

### [v0.5.0] – 2019-10-03 
##### Added
* Multi-variable connections ([PR#295](https://github.com/open-simulation-platform/cse-core/pull/295))
* Support for MSMI extension to FMUs `<FMU-name>_OspModelDescription.xml`, that enables mapping of FMU variables into plugs, sockets and bonds ([PR#333](https://github.com/open-simulation-platform/cse-core/pull/333))
* New CSE configuration format `OspSystemStructure.xml` ([PR#333](https://github.com/open-simulation-platform/cse-core/pull/333)), that enables configuration of: 
    * Individual model step size
    * Plug/socket and bond connections
    * Multi-variable connections. Only sum connection included.
* Can advance an execution to a specific point in time with `cse_execution_simulate_until` ([PR#325](https://github.com/open-simulation-platform/cse-core/pull/325))
* Can override configured step size and start time for fixed-step algorithm in `cse_ssp_fixed_step_execution_create` and `cse_ssp_execution_create` ([PR#373](https://github.com/open-simulation-platform/cse-core/pull/373))

##### Changed
* Must provide instance name when creating local slaves with `cse_local_slave_create`. (Fixing [#381](https://github.com/open-simulation-platform/cse-core/issues/381), [PR#387](https://github.com/open-simulation-platform/cse-core/pull/387))
* `cse_variable_index` renamed to `cse_value_reference` [PR#339](https://github.com/open-simulation-platform/cse-core/pull/339)
   
 ##### Fixed
 * `cse_local_slave_create` is broken when adding more than one instance of the same FMU ([#381](https://github.com/open-simulation-platform/cse-core/issues/381))
 * Memory error when moving a cse::uri ([#361](https://github.com/open-simulation-platform/cse-core/issues/361))
 * Cryptic error message with ill-defined or missing osp:FixedStepMaster in SSP ([#338](https://github.com/open-simulation-platform/cse-core/issues/338))
 * Invalid FMUs in SSP are not properly handled ([#337](https://github.com/open-simulation-platform/cse-core/issues/337))
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
