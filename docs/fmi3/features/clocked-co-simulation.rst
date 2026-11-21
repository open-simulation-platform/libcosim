Clocked Co-Simulation
=====================

Use a scheduled Clock to activate an FMU at specified times, or a
:doc:`connected trigger <clock-and-data-routing>` to relay another FMU's
activation. Both fixed-step and ECCO handle Clock events in Event Mode.

.. glossary::

   Tick
      An activation of a Clock at a simulation time.

   Event Mode
      The FMI lifecycle state in which a Co-Simulation FMU handles events
      and updates its discrete states.

   Event round
      One pass of event propagation and discrete-state updates. Multiple
      rounds can occur at the same physical time (super-dense time).

Schedule an input Clock
-----------------------

.. list-table::
   :header-rows: 1

   * - Interval variability
     - What determines the next tick
   * - ``constant``, ``fixed``
     - A periodic interval. Fixed schedules can be configured before
       initialization only when the model declares ``intervalDecimal``.
       Otherwise the importer queries the interval and shift during
       initialization without setting them.
   * - ``tunable``
     - A periodic interval that can change in Event Mode. Queue
       host-controlled changes while execution is paused; the importer
       refreshes FMU-controlled intervals.
   * - ``changing``, ``countdown``
     - In Event Mode, query a changing Clock after it ticks and after
       updating discrete states, before returning to Step Mode. Query
       countdown Clocks at every event; their interval can be not yet known.
   * - ``triggered``
     - An external activation, not an interval.

For a host-controlled decimal tunable Clock named ``clock_name`` in an
imported FMI 3 ``fmu``, find the reference and configure the instance
**before** adding it to an execution:

.. code-block:: cpp

   const auto& clocks = fmu.clock_descriptions();
   const auto clock = std::find_if(clocks.begin(), clocks.end(),
       [&](const cosim::fmi::v3::clock_description& candidate) {
           return candidate.name == clock_name &&
               candidate.causality == cosim::variable_causality::input &&
               candidate.interval_variability ==
                   cosim::fmi::v3::clock_interval_variability::tunable &&
               candidate.interval_decimal.has_value();
       });
   if (clock == clocks.end()) {
       throw std::runtime_error("Expected a decimal tunable input Clock");
   }
   auto instance = fmu.instantiate_v3_slave("clocked");
   instance->configure_tunable_clock_decimal(clock->reference, 1.0, 0.25);

The first tick is 0.25 s after the start. After initialization, call
``set_tunable_clock_interval_decimal(reference, seconds)`` on the paused
instance to queue a new period; its shift and representation stay fixed.
For a fixed fractional Clock, use
``configure_fixed_clock_fraction(reference, counter, shift, resolution)``.

.. note::

   Decimal ticks round to the nearest nanosecond; fractional ticks round
   upward. The scheduler does not
   accumulate per-tick rounding drift. It rejects unsupported precision,
   out-of-range schedules and updates that would move a tick into the past.
   Clock ``priority`` does not order activations in Co-Simulation.

Clock and data access
---------------------

Clock metadata is available through ``clock_descriptions()``. Dedicated
``get_clocks()`` and ``set_clocks()`` operations query output/local Clocks
and activate input Clocks. Clock variables are not ordinary scalar data
connections. Clock-associated scalar and whole-array values can be accessed
in Initialization Mode or, during Event Mode, while an associated Clock is
active.

Clock event lifecycle
---------------------

For each Clock event, activate the input Clock before writing its associated
values, process outputs, iterate discrete states to convergence, then enter
Step Mode. Discrete-state updates deactivate tracked Clocks. Explicit
deactivation requires the Clock to advertise ``canBeDeactivated``.
For connected models, :doc:`clock-and-data-routing` illustrates the
separate Clock and data paths.

Activate an input Clock directly
--------------------------------

The following function takes an imported FMI 3 FMU. It looks up the
triggered input Clock ``inClock2`` and its associated Int32 input ``input2``
in the model metadata, then activates the Clock in the initial Event Mode.
These names are from the reference ``ClockedCoSimulation.fmu``; use the
names and associations declared by your own FMU.

.. code-block:: cpp

   #include <cosim/fmi/v3/fmu.hpp>
   #include <algorithm>
   #include <stdexcept>

   void activate_initial_clock(cosim::fmi::v3::fmu& fmu)
   {
       // These names belong to the reference ClockedCoSimulation.fmu.
       const auto& clocks = fmu.clock_descriptions();
       const auto clock = std::find_if(clocks.begin(), clocks.end(),
           [](const cosim::fmi::v3::clock_description& candidate) {
               return candidate.name == "inClock2" &&
                   candidate.causality == cosim::variable_causality::input &&
                   candidate.interval_variability ==
                       cosim::fmi::v3::clock_interval_variability::triggered;
           });
       const auto input = cosim::find_variable(*fmu.model_description(), "input2");
       if (clock == clocks.end() || !input ||
           input->type != cosim::variable_type::integer ||
           input->causality != cosim::variable_causality::input ||
           !input->dimensions.empty() ||
           std::find(input->clocks.begin(), input->clocks.end(),
                     clock->reference) == input->clocks.end()) {
           throw std::runtime_error("Expected a triggered Clock and associated Int32 input");
       }

       auto instance = fmu.instantiate_v3_slave("clocked");
       instance->setup(cosim::to_time_point(0.0), std::nullopt, std::nullopt);
       // Clocked instances remain in initial Event Mode after startup.
       instance->start_simulation();
       const cosim::value_reference clock_reference[] = {clock->reference};
       const bool active[] = {true};
       instance->set_clocks(clock_reference, active); // Activate before setting data.
       const cosim::value_reference input_reference[] = {input->reference};
       const int values[] = {7};
       instance->set_integer_variables(input_reference, values);
       // Iterate locally before returning to Step Mode.
       for (std::size_t count = 0; count < 1000; ++count) {
           const auto update = instance->update_discrete_states();
           if (update.terminate_simulation) {
               instance->end_simulation();
               return;
           }
           if (!update.discrete_states_need_update) {
               instance->enter_step_mode();
               instance->end_simulation();
               return;
           }
       }
       throw std::runtime_error("Discrete-state iteration did not converge");
   }

Import the FMU as shown in :doc:`import-and-identity` before calling this
function. During initialization, clocked FMUs remain in initial Event Mode
after ``start_simulation()``. This example terminates immediately after the
initial event; an application continuing to step must retain and schedule
any reported next event time.

Query an output Clock
---------------------

To query an output or local Clock during a permitted Event Mode access,
pass its reference from the FMU's Clock metadata. In Co-Simulation this
operation queries triggered output or local Clocks:

.. code-block:: cpp

   #include <cosim/fmi/v3/fmu.hpp>

   bool output_clock_active(cosim::fmi::v3::slave_instance& instance,
                            cosim::value_reference output_clock)
   {
       // Query an output or local Clock during permitted Event Mode access.
       const cosim::value_reference references[] = {output_clock};
       bool active[] = {false};
       instance.get_clocks(references, active);
       return active[0];
   }
