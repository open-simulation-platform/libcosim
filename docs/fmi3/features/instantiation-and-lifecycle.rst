Instantiation and lifecycle
===========================

``instantiate_v3_slave()`` creates an FMI 3 Co-Simulation instance; the
generic ``instantiate_slave()`` interface is also available. ``setup()``
enters Initialization Mode, ``start_simulation()`` leaves it, and
``end_simulation()`` terminates the instance. Supported configuration,
Event and Step Mode transitions are checked, and ``reset()`` returns a
recoverable instance to its instantiated state. Destruction cleans up
instance resources.

For FMUs with resource files, instantiation supplies the absolute native path
to the extracted ``resources`` directory, including its trailing separator.
When the directory is absent, the FMI call receives a null resource path.
For example, the Reference-FMUs ``Resource.fmu`` reads ``resources/y.txt``
through this path; the caller does not need to construct it.

Event Mode is enabled by default when supported by the FMU. It can be
disabled per instance through ``co_simulation_callbacks::event_mode_used``,
except for FMUs with Clock variables. For a non-clocked Event Mode instance,
initial discrete-state iteration is completed before Step Mode; a clocked
instance remains in Event Mode for initial Clock processing.

Direct callers must set ``co_simulation_callbacks::early_return_allowed``
to opt in to early return on an FMU that supports it. The generic
``instantiate_slave()`` path opts in for event-aware execution.
Use ``fmu::might_return_early_from_do_step()`` and
``fmu::can_return_early_after_intermediate_update()`` to inspect the two
FMU capabilities separately. ``slave_instance::early_return_allowed()``
reports the setting negotiated for that instance.

Example
-------

For a non-clocked FMI 3 FMU ``fmu``, opt out of Event Mode only when the
application intends the FMU to handle its own events:

.. code-block:: cpp

   #include <cosim/fmi/v3/fmu.hpp>

   void run_without_event_mode(cosim::fmi::v3::fmu& fmu)
   {
       cosim::fmi::v3::co_simulation_callbacks options;
       options.event_mode_used = false;
       auto instance = fmu.instantiate_v3_slave("component", options);
       instance->setup(cosim::to_time_point(0.0),
                       cosim::to_time_point(1.0), std::nullopt);
       // Set permitted initialization values here.
       instance->start_simulation();
       // Step and inspect detailed outcomes before termination.
       instance->end_simulation();
   }

The opt-out is invalid for Clock variables. For a direct stepping example
that handles Event Mode and early return, see :doc:`../usage`.

For an independently managed non-clocked instance that has completed a run,
reset it before starting another run. Do not reset an execution-owned instance:

.. code-block:: cpp

   #include <cosim/fmi/v3/fmu.hpp>

   void restart_instance(cosim::fmi::v3::slave_instance& instance)
   {
       instance.reset();
       instance.set_max_event_iterations(200);
       instance.setup(cosim::to_time_point(0.0),
                      cosim::to_time_point(1.0), std::nullopt);
       instance.start_simulation();
       // Step according to the instance's event and Clock requirements.
       instance.end_simulation();
   }

The generic ``instantiate_slave`` method is illustrated in
:doc:`../../design/models-and-fmi`.
