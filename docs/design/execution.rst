Execution and algorithms
========================

Execution lifecycle
-------------------

``execution`` owns the participants and drives an ``algorithm``. It supports
direct assembly with ``add_slave``, ``add_function`` and
``connect_variables``, or injection of a :doc:`system-setup` description.
Add participants and connections before ``initialize()``. ``step()`` returns
the actual duration; ``simulate_until()`` returns false if stopped early.
``is_initialized()`` reports whether initialization completed successfully.
``simulate_until_async()`` and ``stop_simulation()`` support asynchronous
control. A stop immediately following async startup is honored.
``is_running()`` remains true until the loop has finished, even after a stop
request. Keep the execution alive and wait for its future before destroying it
or accessing state. An algorithm instance must not be shared by multiple
executions.

Given a ``std::shared_ptr<cosim::slave> slave`` supplied by the application:

.. code-block:: cpp

   #include <cosim/algorithm/fixed_step_algorithm.hpp>
   #include <cosim/execution.hpp>
   #include <memory>
   #include <stdexcept>

   void run_slave(std::shared_ptr<cosim::slave> slave)
   {
       // Use a separate algorithm for each execution.
       auto algorithm = std::make_shared<cosim::fixed_step_algorithm>(
           cosim::to_duration(0.01));
       cosim::execution run(cosim::to_time_point(0.0), algorithm);
       const auto index = run.add_slave(slave, "model");
       const auto same_slave = run.get_slave(index); // shared ownership
       run.initialize();
       if (!run.is_initialized()) throw std::runtime_error("Initialization failed");
       // The actual advance may be shorter at an event boundary.
       const auto actual_step = run.step();
       const auto time = run.current_time();
       if (!run.simulate_until(cosim::to_time_point(0.1))) {
           throw std::runtime_error("Stopped before the target time");
       }
       // Stop an open-ended run and join it before accessing the execution.
       auto future = run.simulate_until_async(std::nullopt);
       run.stop_simulation();
       const auto still_running = run.is_running();
       const auto reached_target = future.get();
   }

``get_slave(index)`` also returns an instance created by system-structure
injection; use the simulator index from the returned entity-index map.
Configure instance-specific options before initialization. Do not drive the
slave lifecycle or change its values directly while it belongs to an execution:
use the execution and simulator interfaces so cached values stay consistent.

Algorithm choices
-----------------

Both built-in algorithms expose their actual worker count and current event
iteration limit. Fixed-step exposes its base step; ECCO exposes its initial
nominal step, which is not the subsequently adapted step. Custom algorithms
implement ``cosim::algorithm``.

.. _ecco-and-power-bonds:

The :doc:`ECCO and power-bond guide <algorithms/ecco>` explains adaptive
stepping and connections. The :doc:`fixed-step guide <algorithms/fixed-step>`
describes the base interval and decimation. Both algorithms support FMI 1,
FMI 2 and FMI 3 Co-Simulation participants.

.. toctree::
   :maxdepth: 1

   algorithms/fixed-step
   algorithms/ecco
