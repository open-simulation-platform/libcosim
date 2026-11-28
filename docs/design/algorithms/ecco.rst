ECCO and power bonds
====================

Adaptive step loop
------------------

ECCO adapts the step size using scalar Float64 power bonds and controller
parameters. The :ref:`adaptive step figure <fig-ecco-step>` summarizes a
communication interval. An event or Clock can limit the requested step; the
algorithm coordinates work at the boundary and transfers values for the next
interval.
The controller retains its nominal step on boundary-limited steps. An early
return or time event also causes the interrupted interval to be completed
before normal adaptive stepping resumes.

.. _fig-ecco-step:

.. container::

   .. raw:: html

      <figure class="figure-scroll">
        <div class="figure-viewport">
          <object class="stack-figure figure-ecco-flow" type="image/svg+xml" data="../../_static/figures/ecco-step-flow.svg" aria-label="ECCO communication and adaptive step-size loop">
            ECCO plans and steps an interval, measures registered power bonds, adjusts its next nominal step on eligible intervals, coordinates events and transfers outputs.
          </object>
        </div>
        <figcaption>ECCO communication and adaptive step-size loop.</figcaption>
      </figure>

After the initial step, the implementation samples each bond's two powers on
nonzero steps when participants are aligned. For each bond it calculates
``power_a = input_a * output_a``, ``power_b = input_b * output_b`` and
``abs(power_a - power_b)``. Multiplying the residual by the elapsed time
gives an energy residual; the normalized residuals feed a PI controller
whose gain and next nominal step are bounded by the configured parameters.
This is an error indicator, not a guarantee of the simulation's true error.
No bonds or a zero error can leave the step unchanged.

Power-bond connections
----------------------

A power bond identifies two pairs of input/output references; registering it
does **not** make the connections. In the :ref:`power-bond figure <fig-ecco-bond>`,
model A outputs force and receives velocity, while model B receives force
and outputs velocity. The models must use consistent units and sign conventions.

.. _fig-ecco-bond:

.. container::

   .. raw:: html

      <figure class="figure-scroll">
        <div class="figure-viewport">
          <object class="stack-figure figure-ecco-bond" type="image/svg+xml" data="../../_static/figures/ecco-power-bond.svg" aria-label="Two model connections and their ECCO power bond">
            Model A's force output connects to model B's force input; model B's velocity output connects to model A's velocity input. ECCO compares the product of each model's input and output.
          </object>
        </div>
        <figcaption>Force and velocity connections in an ECCO power bond.</figcaption>
      </figure>

Each power is in watts when force is in newtons and velocity in metres per
second. Align the signs and units used by the models; connections or
transformations must represent the intended physical exchange. The four
measured values may differ during a communication step because inputs are
held until the next transfer.

Register a power bond
---------------------

Power bonds can be described in a system structure or registered on the
algorithm before initialization. The following illustrates direct
registration. ``params`` is an application-supplied
``cosim::ecco_algorithm_params``; ``first_slave`` and ``second_slave`` are
compatible participants, and the four references identify their scalar
Float64 power-bond inputs and outputs.

.. code-block:: cpp

   #include <cosim/algorithm/ecco_algorithm.hpp>
   #include <cosim/execution.hpp>
   #include <memory>
   #include <stdexcept>
   #include <string>

   void run_power_bond(const cosim::ecco_algorithm_params& params,
                       std::shared_ptr<cosim::slave> first_slave,
                       const std::string& first_input, const std::string& first_output,
                       std::shared_ptr<cosim::slave> second_slave,
                       const std::string& second_input, const std::string& second_output)
   {
       // Resolve scalar Real inputs and outputs on the supplied participants.
       const auto lookup = [](const cosim::slave& slave, const std::string& name,
                              cosim::variable_causality causality) {
           const auto variable = cosim::find_variable(slave.model_description(), name);
           if (!variable || variable->type != cosim::variable_type::real ||
               variable->causality != causality || !variable->dimensions.empty()) {
               throw std::runtime_error("Expected a scalar Real power-bond endpoint");
           }
           return variable->reference;
       };
       const auto in_a = lookup(*first_slave, first_input, cosim::variable_causality::input);
       const auto out_a = lookup(*first_slave, first_output, cosim::variable_causality::output);
       const auto in_b = lookup(*second_slave, second_input, cosim::variable_causality::input);
       const auto out_b = lookup(*second_slave, second_output, cosim::variable_causality::output);
       auto algorithm = std::make_shared<cosim::ecco_algorithm>(params);
       cosim::execution run(cosim::to_time_point(0.0), algorithm);
       const auto first = run.add_slave(first_slave, "first");
       const auto second = run.add_slave(second_slave, "second");
       const cosim::variable_id first_in{first, cosim::variable_type::real, in_a};
       const cosim::variable_id first_out{first, cosim::variable_type::real, out_a};
       const cosim::variable_id second_in{second, cosim::variable_type::real, in_b};
       const cosim::variable_id second_out{second, cosim::variable_type::real, out_b};
       // Data connections are separate from registering the measured bond.
       run.connect_variables(first_out, second_in);
       run.connect_variables(second_out, first_in);
       algorithm->add_power_bond(first_in, first_out, second_in, second_out);
       run.initialize();
       const auto actual_step = run.step();
   }

Recovery and coordination
--------------------------

ECCO rejects power-bond evaluation when a participant is ahead of the
communication point. After a failed step, restore a complete earlier
execution snapshot or recreate the execution before continuing; a failed
initialization requires recreation. A host Clock interval rejected before
event processing may be corrected and retried.
For connected runtime events, ECCO uses the same lockstep iteration and
floating-point tolerance as fixed-step. Set the tolerance before
initialization with ``set_event_value_tolerance``; see
:doc:`../../fmi3/features/coupled-event-propagation`.
For FMI 3 capability combinations and timeline traces of early return,
rollback, delayed application, common grids and Clock sampling, see
:doc:`../../fmi3/features/execution-algorithms-and-multirate-stepping`.
For execution ownership and controls, see :doc:`../execution`.
