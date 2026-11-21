Fixed-step algorithm
====================

Base step and decimation
------------------------

The fixed-step algorithm advances an execution using a base communication
interval. It can coordinate FMI 1, FMI 2, FMI 3 and compatible custom
time-stepped participants. A positive integer decimation factor lets a
participant use a multiple of the base interval; ordinary connections
between different factors transfer on their common communication grid.

For an application-supplied non-clocked ``slave``, add the participant
before configuring its decimation factor, then initialize the execution:

.. code-block:: cpp

   #include <cosim/algorithm/fixed_step_algorithm.hpp>
   #include <cosim/execution.hpp>
   #include <memory>

   void run_decimated(std::shared_ptr<cosim::slave> slave)
   {
       // This non-clocked participant may use a multiple of the base step.
       auto algorithm = std::make_shared<cosim::fixed_step_algorithm>(
           cosim::to_duration(0.01));
       cosim::execution run(cosim::to_time_point(0.0), algorithm);
       const auto participant = run.add_slave(slave, "slow");
       algorithm->set_stepsize_decimation_factor(participant, 2);
       algorithm->set_max_event_iterations(200);
       algorithm->set_event_value_tolerance({1e-10, 1e-10}); // Absolute, relative.
       run.initialize();
       const auto actual_step = run.step(); // Actual advance can be shorter.
   }

The factor describes the participant's step interval, not a second adaptive
scheduler. ``run.step()`` reports the actual execution advance, which can be
shorter than the base interval at a supported event boundary.

Event and Clock coordination
----------------------------

Fixed-step can coordinate supported FMI 3 Event Mode and Clock work alongside
FMI 1, FMI 2 and non-Event Mode participants. Clock-capable participants
require factor one. For lockstep updates and the floating-point event
tolerance, see :doc:`../../fmi3/features/coupled-event-propagation`.
Event boundaries, delayed transfers, replay and fixed
communication-step restrictions are described with timelines in
:doc:`../../fmi3/features/execution-algorithms-and-multirate-stepping`.
After a failed step, restore a compatible earlier execution snapshot or
recreate the execution before continuing.

See :doc:`../execution` for ownership and execution controls. For adaptive
stepping, see :doc:`ecco`.
