Execution algorithms
====================

Use ``cosim::execution`` to run mixed FMI 1, 2 and 3 Co-Simulation FMUs.
Both built-in algorithms can shorten a requested step at an event or Clock
boundary. ``execution::step()`` returns the actual advance.

.. list-table::
   :header-rows: 1

   * - Algorithm
     - When to use it
   * - :doc:`Fixed-step <../../design/algorithms/fixed-step>`
     - Use a nominal step size, optionally with integer step-size
       decimation for eligible participants.
   * - :doc:`ECCO <../../design/algorithms/ecco>`
     - Adapt the step size from scalar ``Float64`` power bonds. ECCO
       does not support integer decimation.

Use the :doc:`quick start <../usage>` for a fixed-step run. See
:doc:`execution-event-boundaries` for shortened steps and recovery,
or :doc:`multirate-and-clock-sampling` for decimation and data transfer.

Example
-------

For a configured system, make the nominal step size explicit and use the
returned duration to measure progress:

.. code-block:: cpp

   #include <cosim/algorithm/fixed_step_algorithm.hpp>
   #include <cosim/execution.hpp>
   #include <cosim/orchestration.hpp>
   #include <memory>

   cosim::duration advance_once(const cosim::system_structure& system)
   {
       auto algorithm = std::make_shared<cosim::fixed_step_algorithm>(
           cosim::to_duration(0.1));
       cosim::execution run(cosim::time_point{}, algorithm);
       cosim::inject_system_structure(run, system, {});
       run.initialize();
       return run.step();
   }

.. warning::

   An FMU that does not advertise variable communication-step support must
   keep its first successful requested step size. A later request of a
   different size is rejected before that FMU applies staged inputs.
   This can happen when an event splits a step or ECCO adapts its size.
   Other FMUs may already have advanced: restore a complete earlier
   snapshot or recreate the execution after a failed step.
