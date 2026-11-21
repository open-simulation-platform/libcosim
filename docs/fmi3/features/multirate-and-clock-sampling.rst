Multirate steps and Clock samples
=================================

Fixed-step decimation makes an eligible participant step less often than the
base rate. Use it when that participant can accept a larger communication
step and its connections can tolerate less frequent updates. The factor
multiplies the base step; it does not make the participant adaptive.

.. code-block:: cpp

   #include <cosim/algorithm/fixed_step_algorithm.hpp>
   #include <cosim/execution.hpp>
   #include <memory>

   void step_decimated(std::shared_ptr<cosim::slave> slave)
   {
       auto algorithm = std::make_shared<cosim::fixed_step_algorithm>(
           cosim::to_duration(0.01));
       cosim::execution run(cosim::to_time_point(0.0), algorithm);
       const auto sim = run.add_slave(slave, "slow");
       algorithm->set_stepsize_decimation_factor(sim, 2);
       run.initialize();
       run.step();
   }

Use a non-clocked participant in this example. With a 1 s base step and
ordinary participants of factors two and three, their ordinary connection
transfers on the common 6 s grid: the target uses its initial input for
steps starting at 0 and 3 s, then receives the source's 6 s value.

.. _fig-grid-clock-timeline:

.. raw:: html

   <figure class="figure-scroll">
     <div class="figure-viewport">
       <object class="stack-figure figure-grid-timeline" type="image/svg+xml" data="../../_static/figures/common-grid-clock-timelines.svg" aria-label="Common-grid communication and Clock sampling">
         Factors two and three transfer ordinary values on their common six-step grid. An ahead source supplies its last staged value at a Clock tick.
       </object>
     </div>
     <figcaption>Common-grid transfers and held Clock input values.</figcaption>
   </figure>

Sampling a Clock-associated input
---------------------------------

If an ordinary source has advanced from 0 to 2 s and a target Clock ticks at
1 s, the target samples the last valid staged value, not the source's
future output. At 2 s it can sample the current output. This also applies
to eligible function-mediated paths. See :doc:`clock-and-data-routing`.

.. warning::

   Clock-capable participants must use factor one and be at the Clock
   time. A decimated participant without Event Mode cannot schedule time
   events or return early. Termination requests with decimation in use,
   and turning off decimation while a participant remains ahead, are
   rejected. See :doc:`execution-event-boundaries` for replay restrictions.
