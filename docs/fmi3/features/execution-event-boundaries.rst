Execution at event boundaries
=============================

A known time event can shorten a step before an FMU advances. An unexpected
early return changes the communication endpoint after the step starts.
Both algorithms finish the remaining interval before resuming nominal steps.
ECCO excludes interrupted and remainder steps from adaptive-controller
updates; these steps may be shorter than its configured minimum.

Example
-------

Use the execution clock to measure progress across event boundaries:

.. code-block:: cpp

   #include <cosim/execution.hpp>

   cosim::duration advance_at_boundary(cosim::execution& run)
   {
       // The execution must already be initialized.
       const auto before = run.current_time();
       run.step();
       return run.current_time() - before;
   }

Known event
-----------

With a 0.1 s base step, a known event at 0.15 s splits the 0.10-to-0.20 s
interval into two 0.05 s steps. An event already on a communication point
does not add another step.

.. _fig-fixed-step-event:

.. raw:: html

   <figure class="figure-scroll">
     <div class="figure-viewport">
       <object class="stack-figure figure-fixed-step" type="image/svg+xml" data="../../_static/figures/fmi3-fixed-step.svg" aria-label="Fixed-step coordination at an FMI 3 event boundary">
         An event at 0.15 seconds splits the nominal interval into two 0.05-second steps.
       </object>
     </div>
     <figcaption>An event splits a nominal communication step.</figcaption>
   </figure>

Unexpected early return
-----------------------

Suppose an event source stops early at 1.2 s and its output changes from 0
to 7. A peer has already taken a step from 0 to 3 s:

.. list-table::
   :header-rows: 1

   * - Peer state
     - What happens
   * - Usable checkpoint and safe replay
     - The peer replays to 1.2 s with input 0, then continues with input 7.
   * - No state support, or replay is not safe
     - The peer stays at 3 s. A warning reports delayed delivery; its next
       step uses input 7 or a newer staged value. Its output at 3 s is not
       sampled as if it were available at 1.2 s.

An Intermediate Update callback or an active modifier can prevent automatic
replay even when the FMU supports saving state. Replay does not give a peer
Event Mode. If the peer has not stepped yet, it can request the earlier
endpoint without rollback, provided it accepts the shortened step.
An unexpected checkpoint save or restore failure from a state-capable FMU
fails the step; it is not treated as an FMU without state support.

.. _fig-event-replay-timeline:

.. raw:: html

   <figure class="figure-scroll">
     <div class="figure-viewport">
       <object class="stack-figure figure-event-timeline" type="image/svg+xml" data="../../_static/figures/event-multirate-timelines.svg" aria-label="Early return with replay or delayed input delivery">
         At 1.2 seconds a fast source returns early. A peer already at 3 seconds either rewinds or applies the input at its next step.
       </object>
     </div>
     <figcaption>Early return with and without a replayable checkpoint.</figcaption>
   </figure>

Simultaneous events
-------------------

Affected non-clocked FMUs stay in Event Mode at the same physical time.
Outputs that change during propagation reach peers before that instant's
discrete state update. Outputs that change as a result of the update
reach peers at the next super-dense instant. The shared per-time iteration limit fails the step
if a time-based Clock tick is still due; it does not skip that tick.
See :doc:`coupled-event-propagation` for the event API and
:doc:`clock-and-data-routing` for Clock-specific constraints.
