Clock and data routing
======================

Connect a triggered output Clock to one or more triggered input Clocks.
Connect the associated data separately. The execution delivers the sample
when the target Clock is active, before the target updates its discrete states.

.. _fig-clock-routing:

.. container::

   .. raw:: html

      <figure class="figure-scroll">
        <div class="figure-viewport">
          <object class="stack-figure figure-clock-routing" type="image/svg+xml" data="../../_static/figures/fmi3-clock-routing.svg" aria-label="FMI 3 Clock and associated data connections">
            A source output Clock connects to a target input Clock. A separate data connection stages a value associated with that Clock; the target receives it on activation and updates discrete states.
          </object>
        </div>
        <figcaption>Triggered Clock routing and associated data transfer.</figcaption>
      </figure>

Example
-------

This function resolves two instances of the reference
``ClockedCoSimulation.fmu`` from its absolute path, connects a Clock and
its associated data separately, and runs one step:

.. code-block:: cpp

   #include <cosim/algorithm/fixed_step_algorithm.hpp>
   #include <cosim/execution.hpp>
   #include <cosim/orchestration.hpp>
   #include <cosim/system_structure.hpp>
   #include <cosim/uri.hpp>
   #include <memory>

   void route_clocks(const cosim::filesystem::path& path)
   {
       // path points to the reference ClockedCoSimulation.fmu.
       auto resolver = cosim::default_model_uri_resolver();
       auto model = resolver->lookup_model(cosim::path_to_file_uri(path));
       cosim::system_structure system;
       system.add_entity("source", model);
       system.add_entity("target", model);
       // Connect the Clock and its associated data separately.
       system.connect_variables({"source", "outClock"},
                                {"target", "inClock2"});
       system.connect_variables({"source", "inClock1Ticks"},
                                {"target", "input2"});
       auto algorithm = std::make_shared<cosim::fixed_step_algorithm>(
           cosim::to_duration(0.01));
       cosim::execution run(cosim::to_time_point(0.0), algorithm);
       const auto ids = cosim::inject_system_structure(run, system, {});
       run.initialize();
       const auto actual_step = run.step();
   }

These variable names are specific to the reference FMU, not universal FMI
names. For another FMU, inspect its model and Clock metadata for compatible
endpoints. The router stages
data before the target Clock is activated; a Clock connection does not
replace its associated data connection.

Constraints
-----------

.. list-table::
   :header-rows: 1

   * - Case
     - Behavior
   * - Ordinary values and independently scheduled Clocks
     - Either side of a data path can be Clock-associated. The target holds
       its last sample until the next activation. Eligible function paths
       follow the same rule.
   * - Attaching a schedule without a source FMU
     - Define an OSP 0.2.0 Clock source and connect it to a triggered input.
       See :doc:`system-structure-configuration`.
   * - Multirate execution
     - Clock-capable FMUs must use decimation factor one and reach the Clock
       time. An ahead ordinary source supplies its last valid sample, with a
       warning; see :doc:`multirate-and-clock-sampling`.
   * - Clock feedback and retraction
     - Cyclic Clock-associated data feedback, changes to data already
       delivered in the same sweep, oscillating retractions and retraction
       of inputs that cannot be deactivated are rejected.

.. warning::

   If Clock processing fails after entering Event Mode, restore an earlier
   complete execution snapshot or start a new execution.
