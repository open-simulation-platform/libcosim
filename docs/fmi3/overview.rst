FMI 3 overview
==============

libcosim supports a subset of `FMI 3.0.2 <https://fmi-standard.org/docs/3.0.2/>`_
Co-Simulation. Model Exchange and Scheduled Execution are not supported.
Start with :doc:`usage`, or browse the :doc:`implemented` feature reference.

.. list-table::
   :header-rows: 1
   :widths: 35 65

   * - Capability
     - What to check
   * - :doc:`Scalars and arrays <features/whole-arrays-and-structural-parameters>`
     - Use exact types and whole arrays. Runtime resizing requires a tunable
       structural parameter and Step Mode.
   * - :doc:`Events and Clocks <features/clocked-co-simulation>`
     - Clocks require Event Mode. The FMU must support the requested
       operations.
   * - :doc:`File configuration <features/system-structure-configuration>`
     - OSP 0.2.0 and SSP 2.0 support exact values and fixed-shape arrays.
       OSP 0.2.0 can also define a Clock source for a triggered FMU input.
   * - :doc:`Stepping <features/stepping-and-status-handling>`
     - Early return must be negotiated. Events reported away from a completed
       communication point are not supported without early return.
   * - :doc:`System execution <features/execution-algorithms-and-multirate-stepping>`
     - Fixed-step and ECCO coordinate supported events. If a participant
       cannot rewind, some events can be delivered late.

Execution flow
--------------

The :ref:`execution flow figure <fig-fmi3-flow>` shows when optional
configuration, Event Mode and Clock handling enter an FMI 3 run.
Select a box for the relevant feature guide. On narrow screens, scroll
horizontally. The :doc:`implemented` index also lists the linked guides.

.. _fig-fmi3-flow:

.. container::

   .. raw:: html

      <figure class="figure-scroll">
        <div class="figure-viewport">
          <object class="stack-figure figure-fmi3" type="image/svg+xml" data="../_static/figures/fmi3-execution-flow.svg" aria-label="FMI 3 Co-Simulation execution flow and feature map">
            An FMI 3 instance is instantiated, optionally configured, initialized, stepped and terminated. Event Mode and Clock processing are used when supported and enabled.
          </object>
        </div>
        <figcaption>FMI 3 Co-Simulation execution flow.</figcaption>
      </figure>

The :doc:`../architecture` guide explains the layers, and
:doc:`../design/workflows` compares the FMI 1, 2 and 3 simulation paths.
