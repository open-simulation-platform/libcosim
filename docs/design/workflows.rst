Simulation workflows by FMI version
===================================

The built-in algorithms coordinate FMI Co-Simulation participants through
the same execution and simulator interfaces. The
:ref:`workflow figure <fig-fmi-workflow>` follows a local FMU
from import through release and labels the version-specific steps.
The :doc:`../architecture` figure identifies the libraries used by each
layer. A custom slave or the optional Proxy-FMU remote path can replace the
local FMI participant; neither is a call through the local importer.
On narrow screens, scroll the diagram horizontally to see all its details.

For a complete application, build and link libcosim and supply a compatible
Co-Simulation FMU as described in :doc:`../getting_started`. That example
uses the version-detecting importer and works with a suitable FMI 1, 2 or 3
FMU; FMI 3 Clock and Event Mode cases require the additional handling
described in :doc:`../fmi3/usage`.

.. _fig-fmi-workflow:

.. container::

   .. raw:: html

      <figure class="figure-scroll">
        <div class="figure-viewport">
          <object class="stack-figure figure-workflow" type="image/svg+xml" data="../_static/figures/workflow-fmi.svg" aria-label="Shared FMI 1, 2 and 3 Co-Simulation workflow and version-specific steps">
            Import and instantiate an FMU, initialize it, advance and observe it, then terminate and release it. FMI 1 initializes after initial value exchange; FMI 2 and 3 exchange values in Initialization Mode. FMI 3 also supports optional Configuration Mode and bounded event and Clock handling.
          </object>
        </div>
        <figcaption>Co-Simulation workflow for FMI 1, 2 and 3.</figcaption>
      </figure>

Initialization differences
--------------------------

For FMI 1, libcosim records the time bounds and exchanges initial values
before initializing the slave. FMI 1 has no Initialization Mode. For FMI 2,
libcosim sets up the experiment, enters Initialization Mode, exchanges
initial values and exits the mode. FMI 3 enters Initialization Mode with the
time bounds, exchanges values and exits the mode. If Event Mode is enabled,
initial events are processed before stepping; clocked FMUs initially remain
in Event Mode. See :doc:`models-and-fmi` and :doc:`execution` for the common
interfaces and :doc:`../getting_started` for a complete application.

FMI 3 features
--------------

FMI 3 supports exact typed scalar and whole-array values, optional
Configuration Mode, and bounded Co-Simulation event and Clock handling.
Applications using the detailed FMI 3 instance API must inspect step outcomes,
including early return and termination. State operations for FMI 2 and 3
require the corresponding FMU capabilities. See :doc:`../fmi3/overview` and
:doc:`../fmi3/implemented` for lifecycle and feature-specific constraints.
