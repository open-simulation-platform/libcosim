Implemented FMI 3 features
==========================

The features below describe the current bounded Co-Simulation implementation.
Optional operations depend on the FMU's advertised capabilities and valid
lifecycle state. Task pages lead with the behavior and prerequisites, then
show an example and any constraints. Examples that use ``instance`` assume an imported FMI 3
``slave_instance``; references must come from the selected FMU's metadata,
not be copied blindly from another FMU. See :doc:`usage` for a complete
non-clocked import and step.

The examples are C++17 fragments, not all standalone programs. Direct FMI 3
examples use ``<cosim/fmi/v3/fmu.hpp>`` and ``<cosim/fmi/importer.hpp>``;
execution examples use ``<cosim/execution.hpp>`` and the relevant algorithm,
observer, manipulator or configuration header. Include the standard-library
headers for the types used in your application.

.. toctree::
   :maxdepth: 1

   features/import-and-identity
   features/model-and-variable-metadata
   features/model-structure-and-dependencies
   features/exact-scalar-values
   features/whole-arrays-and-structural-parameters
   features/enumeration-values
   features/instantiation-and-lifecycle
   features/stepping-and-status-handling
   features/logging
   features/event-mode-and-input-events
   features/coupled-event-propagation
   features/clocked-co-simulation
   features/clock-and-data-routing
   features/intermediate-update
   features/fmu-state-and-execution-snapshots
   features/derivatives
   features/execution-algorithms-and-multirate-stepping
   features/execution-event-boundaries
   features/multirate-and-clock-sampling
   features/connections-and-functions
   features/observers-modifiers-and-scenarios
   features/system-structure-configuration
