Time, logging and state
=======================

``time_point`` and ``duration`` represent logical time in nanoseconds.
Conversions from floating-point seconds can round or truncate; calculate
durations consistently at nonzero start times. Logging provides a global
severity filter and a simple console sink, including forwarded FMI messages.
For per-instance FMI 3 category control, see
:doc:`../fmi3/features/logging`.

.. code-block:: cpp

   #include <cosim/log/simple.hpp>
   #include <cosim/time.hpp>

   // Set up logging once and express logical time in seconds.
   cosim::log::setup_simple_console_logging();
   cosim::log::set_global_output_level(cosim::log::info);
   const auto start = cosim::to_time_point(0.0);
   const auto step = cosim::to_duration(0.01);
   const auto next = start + step;
   const double seconds = cosim::to_double_time_point(next);

For an initialized, paused execution ``run`` whose participants and
algorithm support serialization, a snapshot can be restored into the same
unchanged system structure:

.. code-block:: cpp

   #include <cosim/execution.hpp>

   void restore_after_step(cosim::execution& run)
   {
       // The paused execution and its participants must support serialization.
       const auto checkpoint = run.export_current_state();
       run.step();
       run.import_state(checkpoint);
   }

State support is capability- and context-dependent; functions and active
modifiers impose additional restrictions in some algorithms. A saved state
is not a system description. See
:doc:`../fmi3/features/fmu-state-and-execution-snapshots`.
