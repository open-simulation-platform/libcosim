# Change log
All notable changes to cse-core will be documented in this file. This includes new features, bug fixes and breaking changes. For a more detailed list of all changes, click the header links for each version. 

This project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html)


### [v0.3.0] – 2019-06-26 
##### Added
* fmu-proxy integration
* introducing`orchestration` interface for classes that resolve model URIs of one or more specific URI schemes 
* logging configuration
* observers can observe string and boolean variables
* can set arbitraty real time factor
* improved error reporting
   
##### Fixed
* Prevent crash when FMU GUIDs include curly brackets
* Prevent crash when FMU includes enumerated variables

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
