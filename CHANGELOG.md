# Change log
All notable changes to cse-core will be documented in this file. This includes new features, bug fixes and breaking changes. For a more detailed list of all changes, click the header links for each version. 

This project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html)

### [v0.4.0] – 2019-08-19 
##### Added
* Initial variable values configurable from SSP ([PR#291](https://github.com/open-simulation-platform/cse-core/pull/291))
* Allow boolean to be set from scenario ([PR#292](https://github.com/open-simulation-platform/cse-core/pull/292))
* Providing overview of modified variables ([PR#296](https://github.com/open-simulation-platform/cse-core/pull/296))
* Give client code more control over logging ([PR#314](https://github.com/open-simulation-platform/cse-core/pull/314))
   
##### Fixed
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
