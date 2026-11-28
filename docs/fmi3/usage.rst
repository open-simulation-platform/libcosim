Using FMI 3 Co-Simulation
=========================

Quick start: run one FMU
------------------------

Use the execution API when you want libcosim to handle stepping and events.
Pass the absolute path to an FMI 3 Co-Simulation FMU that can run with its
default inputs and parameters.

.. code-block:: cpp

   #include <cosim/algorithm/fixed_step_algorithm.hpp>
   #include <cosim/execution.hpp>
   #include <cosim/orchestration.hpp>
   #include <cosim/system_structure.hpp>
   #include <cosim/uri.hpp>
   #include <memory>

   bool run_one_fmu(const cosim::filesystem::path& path)
   {
       auto resolver = cosim::default_model_uri_resolver();
       cosim::system_structure system;
       system.add_entity("model", resolver->lookup_model(cosim::path_to_file_uri(path)));
       auto algorithm = std::make_shared<cosim::fixed_step_algorithm>(
           cosim::to_duration(0.01));
       cosim::execution run(cosim::to_time_point(0.0), algorithm);
       cosim::inject_system_structure(run, system, {});
       run.initialize();
       return run.simulate_until(cosim::to_time_point(1.0));
   }

.. note::

   ``false`` means execution stopped before the requested end time.
   If the FMU needs initial values or Clock configuration, see
   :doc:`../design/system-setup` and :doc:`features/clock-and-data-routing`.

Run a connected system
----------------------

Use ``execution`` to coordinate stepping and supported events across FMUs.
This example takes two absolute FMU paths. The source must have an ``output``
variable compatible with the target's ``input``; substitute the actual names.

.. code-block:: cpp

   #include <cosim/algorithm/fixed_step_algorithm.hpp>
   #include <cosim/execution.hpp>
   #include <cosim/orchestration.hpp>
   #include <cosim/system_structure.hpp>
   #include <cosim/uri.hpp>
   #include <memory>

   bool run_connected(const cosim::filesystem::path& source_path,
                      const cosim::filesystem::path& target_path)
   {
       auto resolver = cosim::default_model_uri_resolver();
       cosim::system_structure system;
       system.add_entity("source", resolver->lookup_model(cosim::path_to_file_uri(source_path)));
       system.add_entity("target", resolver->lookup_model(cosim::path_to_file_uri(target_path)));
       system.connect_variables({"source", "output"}, {"target", "input"});

       auto algorithm = std::make_shared<cosim::fixed_step_algorithm>(
           cosim::to_duration(0.01));
       cosim::execution run(cosim::to_time_point(0.0), algorithm);
       cosim::inject_system_structure(run, system, {});
       run.initialize();
       return run.simulate_until(cosim::to_time_point(1.0));
   }

A ``false`` result means the run stopped before reaching the target.
See :doc:`../design/system-setup` for initial values and instantiation options,
or :doc:`features/system-structure-configuration` for OSP and SSP setup.

Import, instantiate and step
----------------------------

This example uses a **non-clocked** FMI 3 Co-Simulation FMU. The result of
``do_step_with_info`` must be examined instead of assuming that the
requested endpoint was reached. Error handling here is deliberately minimal.

.. code-block:: cpp

   #include <cosim/fmi/importer.hpp>
   #include <cosim/fmi/v3/fmu.hpp>
   #include <cosim/time.hpp>
   #include <memory>
   #include <stdexcept>

   void run_one_step(const cosim::filesystem::path& path)
   {
       // This direct lifecycle example requires a non-clocked FMI 3 FMU.
       auto importer = cosim::fmi::importer::create();
       auto fmu = std::dynamic_pointer_cast<cosim::fmi::v3::fmu>(
           importer->import(path));
       if (!fmu || !fmu->clock_descriptions().empty()) {
           throw std::runtime_error("Expected a non-clocked FMI 3 FMU");
       }
       cosim::fmi::v3::co_simulation_callbacks options;
       options.early_return_allowed = true;
       auto instance = fmu->instantiate_v3_slave("example", options);
       const auto start = cosim::to_time_point(0.0);
       instance->setup(start, cosim::to_time_point(1.0), std::nullopt);
       instance->start_simulation();
       const auto event_status = instance->get_event_handling_status();
       if (event_status.termination_requested) {
           instance->end_simulation();
           return;
       }
       // Do not step past the next event reported during initialization.
       auto step_size = cosim::to_duration(0.1);
       if (event_status.next_event_time &&
           *event_status.next_event_time <= start) {
           throw std::runtime_error("A scheduled event is already due");
       }
       if (event_status.next_event_time &&
           *event_status.next_event_time < start + step_size) {
           step_size = *event_status.next_event_time - start;
       }
       const auto result = instance->do_step_with_info(
           start, step_size);
       if (result.result == cosim::step_result::failed) {
           throw std::runtime_error("Step failed; inspect retryability");
       }
       // An early return changes the communication endpoint.
       const auto reached = result.early_return
           ? result.last_successful_time : start + step_size;
       const bool scheduled_event = event_status.next_event_time &&
           reached == *event_status.next_event_time;
       // Handle requested or scheduled events before any subsequent step.
       if (!result.termination_requested &&
           (result.event_handling_needed || scheduled_event)) {
           instance->handle_events();
       }
       instance->end_simulation();
   }

Use ``fmu->model_description()`` as shown in
:doc:`features/model-and-variable-metadata` to select variables and their
exact types, causality and dimensions. For a longer run, use the actual reported
endpoint for an early return, process events before stepping again, and stop
when termination is requested. Schedule advertised time events rather than
stepping past them. When no early return occurs, the reported
internal time is not necessarily the requested communication endpoint.
An ``execution`` with a built-in algorithm coordinates these details for a
connected system.

Values and arrays
-----------------

Exact typed scalar functions take value-reference and value spans. Whole-value
access also supports arrays. :doc:`features/exact-scalar-values` shows how
to retrieve named variables from an instance's metadata and use their
references. :doc:`features/whole-arrays-and-structural-parameters` shows
configuration, shape resolution and a whole-array write. Writes must be valid
in the current FMI mode; resolve dynamic dimensions again after structural
changes.

Event Mode and Clocks
---------------------

Event Mode
~~~~~~~~~~

By default, Event Mode is enabled when the FMU supports it. To opt out for
a non-clocked FMU, pass ``co_simulation_callbacks`` with
``event_mode_used = false`` to ``instantiate_v3_slave``. Clock variables
require Event Mode.

Clock event iteration
~~~~~~~~~~~~~~~~~~~~~

For direct control of a clocked instance, configure a fixed input Clock
before initialization if necessary. The
:doc:`features/clocked-co-simulation` example retrieves a Clock reference from
metadata and configures a tunable Clock.
After initialization, process the initial Event Mode, activate input Clocks,
read or set associated values while active, call
``update_discrete_states()`` until it converges, and enter Step Mode.
On later ticks enter Event Mode first. Inspect termination and next-event
flags during iteration. The fixed-step and ECCO algorithms perform their
supported Clock coordination for an execution.

For example, after activating the required input Clocks in Event Mode, a
direct caller can complete the FMU-local iteration as follows:

.. code-block:: cpp

   #include <cosim/fmi/v3/fmu.hpp>
   #include <stdexcept>

   std::optional<cosim::time_point> finish_event(
       cosim::fmi::v3::slave_instance& instance)
   {
       // Start in Event Mode with the required input Clocks activated.
       std::optional<cosim::time_point> next_event;
       for (std::size_t count = 0; count < 1000; ++count) {
           const auto update = instance.update_discrete_states();
           if (update.terminate_simulation) {
               throw std::runtime_error("FMU requested termination");
           }
           next_event = update.next_event_time;
           if (!update.discrete_states_need_update) {
               // Return to Step Mode only after local convergence.
               instance.enter_step_mode();
               return next_event;
           }
       }
       throw std::runtime_error("Discrete-state iteration did not converge");
   }

Schedule the returned time, when present, as a future Event Mode boundary.

Intermediate Update callbacks
-----------------------------

Intermediate Update callbacks are registered at instantiation. See
:doc:`features/intermediate-update` for a metadata lookup and callback
example. Access only registered variables when the callback flags allow it,
and perform Gets before Sets.

Decimation and event limits
---------------------------

Configure ``fixed_step_algorithm::set_stepsize_decimation_factor`` after
adding a simulator to the algorithm. A Clock-capable simulator must use
factor one. Both built-in algorithms offer ``set_max_event_iterations``;
set a positive value before coordination starts. The default is 1000 coupled
rounds at each physical time.

For a non-clocked ``slave`` supplied by the application, configure these
settings before initializing the execution. Include
``<cosim/algorithm/fixed_step_algorithm.hpp>`` and ``<cosim/execution.hpp>``:

.. code-block:: cpp

   #include <cosim/algorithm/fixed_step_algorithm.hpp>
   #include <cosim/execution.hpp>
   #include <memory>

   void configure_decimation(std::shared_ptr<cosim::slave> slave)
   {
       // A non-clocked participant may use twice the base interval.
       auto algorithm = std::make_shared<cosim::fixed_step_algorithm>(
           cosim::to_duration(0.01));
       cosim::execution execution(cosim::to_time_point(0.0), algorithm);
       const auto simulator = execution.add_slave(slave, "slow");
       algorithm->set_stepsize_decimation_factor(simulator, 2);
       algorithm->set_max_event_iterations(200);
       // Initialize and run the execution here.
   }

State, derivatives and dependencies
------------------------------------

FMU state
~~~~~~~~~

Check ``simulator_capabilities`` before calling ``save_state()``,
``restore_state()``, or serialization operations. Release saved state when
it is no longer needed. State saving can be unavailable while inputs or
modifiers are pending.

See :doc:`features/fmu-state-and-execution-snapshots` for an example.

Derivatives and dependencies
~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Use ``provides_directional_derivatives()``,
``provides_adjoint_derivatives()`` and ``max_output_derivative_order()``
before the corresponding derivative calls. Directional seeds are flattened
known elements; adjoint seeds are flattened unknown elements.
``get_variable_dependencies(reference)`` requires the per-element dependency
capability. See the API reference for signatures and allowed lifecycle modes.

For a scalar directional query with metadata-derived references, see
:doc:`features/derivatives`. :doc:`features/model-structure-and-dependencies`
shows how to inspect declared and runtime dependencies.
