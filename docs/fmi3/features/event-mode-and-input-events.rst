Event Mode and input events
===========================

Local event iteration
---------------------

For initial events and direct calls to ``handle_events``, the FMU enters or
stays in Event Mode and performs ``UpdateDiscreteStates``
until convergence, tracks termination and the next event time, and returns
to Step Mode. FMU-local iteration defaults to the shared
``cosim::default_max_event_iterations`` limit of 1000 updates; nonconvergence
is an error. An instance used directly can set its positive limit with
``set_max_event_iterations`` before setup. Fixed-step and ECCO pass their
configured limit to their participants before setup, including initial Event
Mode iteration, and use the same threshold for coupled event and Clock work.
For connected runtime events, fixed-step and ECCO instead coordinate
the affected non-clocked FMUs together, propagating values between discrete
state updates before the FMUs return to Step Mode. See
:doc:`coupled-event-propagation` for the ordering and tolerances.

Input discontinuities
---------------------

Fixed-step and ECCO stage changed discrete inputs and tunable parameters for
Event Mode. A known jump in an exposed, non-clocked continuous input can be
signaled explicitly. An optional per-input policy instead treats every
changed effective sample as a possible discontinuity; it may create extra
events for smooth inputs and does not locate jumps between communication
points. Pending inputs or discontinuities prevent state save and restore.

Example
-------

Detect changed input samples
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

An application implementing a custom manipulator receives a
``cosim::manipulable& participant``. Before simulation starts, it can look
up a named continuous Real input, expose it and opt its changed samples into
event detection:

.. code-block:: cpp

   #include <cosim/manipulator/manipulator.hpp>
   #include <stdexcept>
   #include <string>

   void watch_input(cosim::manipulable& participant, const std::string& name)
   {
       // Expose an unclocked continuous input before enabling detection.
       const auto input = cosim::find_variable(participant.model_description(), name);
       if (!input || input->type != cosim::variable_type::real ||
           input->causality != cosim::variable_causality::input ||
           input->variability != cosim::variable_variability::continuous ||
           !input->dimensions.empty() || !input->clocks.empty()) {
           throw std::runtime_error("Expected an unclocked continuous Real input");
       }
       const auto reference = input->reference;
       participant.expose_for_setting(cosim::variable_type::real, reference);
       // Every changed effective sample can now trigger an input event.
       participant.set_input_sample_discontinuity_policy(
           cosim::variable_type::real, reference, true);
   }

Signal a known jump
~~~~~~~~~~~~~~~~~~~

Alternatively, signal a known jump at a communication point through the
same execution-owned manipulator view, after exposing the input:

.. code-block:: cpp

   #include <cosim/manipulator/manipulator.hpp>

   void signal_jump(cosim::manipulable& participant,
                    cosim::value_reference exposed_input)
   {
       // Signal a known jump at a communication point on an exposed input.
       participant.signal_input_discontinuity(
           cosim::variable_type::real, exposed_input);
   }

Built-in execution algorithms stage and coordinate the supported events.
Direct FMI callers instead process
``do_step_with_info`` and ``handle_events`` as shown in :doc:`../usage`.
