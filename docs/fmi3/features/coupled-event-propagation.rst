Coupled event propagation
=========================

Connected participants
----------------------

Changed event outputs propagate through direct connections and eligible
function chains, including multi-input paths and feedback, at the same
physical time. The built-in linear transformation and vector sum functions
support repeated event evaluation; custom functions must opt in. FMI 1/2
and FMI 3 instances without Event Mode receive staged changes at their next
normal step. The :ref:`connected models <fig-event-models>` identify the
participants in the :ref:`event coordination figure <fig-event-coordination>`.
Non-clocked input, model and time events use the same coordination. At each
super-dense instant, affected FMUs stay in Event Mode while the algorithm
propagates changed outputs in dependency order and repeats propagation
where needed. Once the inputs that can be delivered at this instant settle,
the algorithm calls ``UpdateDiscreteStates`` once per participating FMU.
It then propagates the resulting outputs. Another instant at the same
physical time follows if an FMU still needs an update, a time event is still
due, or propagation produced new event inputs. Participants return to Step
Mode only after this work settles or reaches the iteration guard.

A second set of an input that declares
``canHandleMultipleSetPerTimeInstant=false`` waits for the next super-dense
instant. For such loops, the algorithm does not claim a fixed point at the
original instant. Clock events use a separate routing order; see
:doc:`clock-and-data-routing`.

For example, A and B use Event Mode; C receives changes on its next valid
normal step.

.. _fig-event-models:

.. container::

   .. raw:: html

      <figure class="figure-scroll">
        <div class="figure-viewport">
          <object class="stack-figure figure-coupled-event-models" type="image/svg+xml" data="../../_static/figures/fmi3-coupled-event-models.svg" aria-label="Event-capable participants connected to an ordinary peer">
            Event-capable FMI 3 models A and B connect to an ordinary peer C without Event Mode.
          </object>
        </div>
        <figcaption>Connected Event Mode models A and B and ordinary peer C.</figcaption>
      </figure>

.. _fig-event-coordination:

.. container::

   .. raw:: html

      <figure class="figure-scroll">
        <div class="figure-viewport">
          <object class="stack-figure figure-event-coordination" type="image/svg+xml" data="../../_static/figures/fmi3-event-coordination.svg" aria-label="FMI 3 Event Mode and coupled event coordination">
            Affected FMUs stay in Event Mode across propagation and update rounds at the same physical time. Peers without Event Mode receive staged values on their next valid step.
          </object>
        </div>
        <figcaption>Coupled propagation of an ordinary input event.</figcaption>
      </figure>

Iteration budget and replay safety
----------------------------------

Both algorithms share a configurable coupled event and Clock budget per
physical time, defaulting to 1000 rounds. For ordinary events, each
super-dense instant consumes a round, and the same threshold separately
bounds the propagation sweeps within an instant. At the limit they warn, retain the
last evaluated results, defer pending input events and may drop undelivered
Clock ticks; the result is not convergence. For ordinary events the warning
identifies unfinished FMUs or pending inputs and includes recent changed
values when available. FMUs that cannot finish local iteration and Clock
retractions that exceed the configured threshold fail on
nonconvergence. Repeated zero-progress early returns also fail after the
limit. Startup warns when connected event-capable participants have peers
without Event Mode, state restore or safe automatic replay. Such peers can
receive an event only at their next valid communication point.
Reevaluating an input modifier during
coupled iteration or for a shortened step requires an explicit replay-safe
declaration: the modifier must be deterministic and without externally
visible side effects.

Floating-point event comparisons
--------------------------------

Both algorithms default to an absolute tolerance of ``1e-10`` and a
relative tolerance of ``1e-10``. Two finite Float32 or Float64 samples
compare as unchanged when
``abs(a - b) <= absolute + relative * max(abs(a), abs(b))``. Array elements
are compared individually. Non-floating values still compare exactly.
This only controls change detection in event routing, including the
Clock router's check for a change after delivery; it does not round
values sent to FMUs or change ordinary step transfers. Set both tolerances
to zero before initialization to restore exact comparison.

Example
-------

Given two compatible event-capable model instances, the execution routes a
connected discrete output to the receiving input during event coordination:

.. code-block:: cpp

   #include <cosim/algorithm/fixed_step_algorithm.hpp>
   #include <cosim/execution.hpp>
   #include <memory>
   #include <stdexcept>
   #include <string>

   void run_events(std::shared_ptr<cosim::slave> source_slave,
                   const std::string& output_name,
                   std::shared_ptr<cosim::slave> target_slave,
                   const std::string& input_name)
   {
       // Choose compatible event-capable instances and Integer endpoints.
       const auto output = cosim::find_variable(source_slave->model_description(), output_name);
       const auto input = cosim::find_variable(target_slave->model_description(), input_name);
       if (!output || !input || output->type != cosim::variable_type::integer ||
           input->type != cosim::variable_type::integer ||
           output->causality != cosim::variable_causality::output ||
           input->causality != cosim::variable_causality::input) {
           throw std::runtime_error("Expected compatible Integer endpoints");
       }
       auto algorithm = std::make_shared<cosim::fixed_step_algorithm>(
           cosim::to_duration(0.01));
       algorithm->set_max_event_iterations(200); // Safety budget, not convergence.
       algorithm->set_event_value_tolerance({1e-10, 1e-10}); // Absolute, relative.
       cosim::execution run(cosim::to_time_point(0.0), algorithm);
       const auto source = run.add_slave(source_slave, "source");
       const auto target = run.add_slave(target_slave, "target");
       run.connect_variables(
           cosim::variable_id{source, cosim::variable_type::integer, output->reference},
           cosim::variable_id{target, cosim::variable_type::integer, input->reference});
       // The algorithm coordinates event propagation during execution.
       run.initialize();
       run.step();
   }

The references and type must match the FMUs' metadata. The limit is a safety
budget, not a guarantee of convergence; monitor warnings.
