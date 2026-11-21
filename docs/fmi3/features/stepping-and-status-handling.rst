Stepping and status handling
============================

Step outcomes
-------------

``do_step_with_info()`` reports success or failure, the FMU's last successful
internal time, early return, event-handling and termination requests, and
retryability. The internal time is not necessarily the requested
communication endpoint when no early return occurs. A pending event or
termination request must be handled before another step. The legacy
``do_step()`` rejects instances using Event Mode or early return because it
cannot represent those results.

If a completed step requests an event, handle it at the communication
endpoint even when the FMU reports a different internal time. Only an early
return shortens the communication step.

Some FMUs incorrectly report ``earlyReturn=true`` at the requested endpoint.
When early return was negotiated, libcosim logs one warning per instance and
treats that response as a completed step, while still honoring an event or
termination request. A reported early-return time beyond the endpoint or an
unnegotiated early return remains an error.

FMI statuses
------------

FMI Warning remains visible in logging. A ``DoStep`` Discard is a failed,
retryable step and its undefined output arguments are not used. Discard
from other operations is reported to the caller without advancing the FMI
lifecycle. FMI Error puts the instance in Terminated: diagnostic reads
remain available, and the importer can restore a saved state or reset the
instance. Do not call ``end_simulation()`` after Error. Fatal invalidates
sibling instances of the same imported FMU, and cleanup avoids further
unsafe FMI calls.

Example
-------

Detailed stepping
~~~~~~~~~~~~~~~~~

For an initialized non-clocked ``instance``, use the current communication
time and a positive duration. This fragment handles a requested event; a
complete loop must also honor the next scheduled event time and termination:

.. code-block:: cpp

   #include <cosim/fmi/v3/fmu.hpp>
   #include <stdexcept>

   cosim::time_point step_once(cosim::fmi::v3::slave_instance& instance,
                               cosim::time_point current, cosim::duration step)
   {
       const auto result = instance.do_step_with_info(current, step);
       if (result.result == cosim::step_result::failed) {
           // Retry only when result.retryable and the application can do so.
           throw std::runtime_error("FMU step failed");
       }
       if (result.result != cosim::step_result::complete) {
           throw std::runtime_error("FMU step did not complete");
       }
       const auto reached = result.early_return
           ? result.last_successful_time : current + step;
       if (result.termination_requested) {
           throw std::runtime_error("FMU requested termination");
       }
       if (result.event_handling_needed) {
           instance.handle_events();
       }
       return reached;
   }

Never issue another step after termination. See :doc:`../usage` for the
initial event-time check and a complete one-step lifecycle.

Legacy stepping
~~~~~~~~~~~~~~~

The legacy method is suitable only when neither Event Mode nor early return
is enabled for the initialized instance:

.. code-block:: cpp

   #include <cosim/fmi/v3/fmu.hpp>
   #include <stdexcept>

   cosim::step_result legacy_step(cosim::fmi::v3::slave_instance& instance,
                                  cosim::time_point current, cosim::duration step)
   {
       if (instance.event_mode_used() || instance.early_return_allowed()) {
           throw std::runtime_error("Use do_step_with_info for this instance");
       }
       return instance.do_step(current, step);
   }

Inspect the returned result before advancing the application time.
