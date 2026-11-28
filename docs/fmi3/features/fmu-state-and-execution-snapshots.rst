FMU state and execution snapshots
=================================

When advertised by the FMU, state operations save, overwrite, restore and
release an FMU state; serialization support additionally enables export and
import. Saved host state includes lifecycle, communication and stop times,
event information, Clock schedules and pending interval work, and the values
used to resolve array dimensions. Serialized state validates the scheme,
Co-Simulation interface, instantiation token and timing data. A saved state
from before FMI Error can restore both the FMU and its host bookkeeping.

Supported fixed-step and ECCO snapshots retain event budgets and Clock
schedules; ECCO also retains adaptive-controller and power-bond history.
Both preserve participant coordination; fixed-step FMUs also retain their
first requested communication-step size. Older simulator snapshots lacking
that size are rejected for fixed-step FMUs, while variable-step FMUs can still
read them. A snapshot's event budget must match the configured algorithm
limit. Earlier ECCO snapshots without participant times remain readable; on
the next step, each participant is assumed to be at the restored execution
time, as required by those older aligned-only snapshots.
State serialization is required. Event-aware snapshots containing functions,
ECCO snapshots with active modifiers, snapshots after Clock termination and
decimation snapshots depending on an unsaved rewind checkpoint are rejected.
State operations can also be unavailable while inputs, discontinuities or
modifiers are pending.

After a failed algorithm step, participants may have advanced by different
amounts. Further steps and new algorithm snapshots are rejected; restore a
complete execution snapshot saved before the failure, or recreate the
execution. A pending host Clock interval rejected before event processing can
instead be corrected and retried. An algorithm used directly must be restored
together with all of its participants. A failed ``simulate_until`` leaves the
execution stopped so that a supported restore can be performed.
If initialization fails, recreate the execution rather than retrying a
partially initialized collection of participants.

Example
-------

Pass an FMI 3 instance after setup and check its model capability before
saving:

.. code-block:: cpp

   #include <cosim/fmi/v3/fmu.hpp>

   void save_and_restore(cosim::fmi::v3::slave_instance& instance)
   {
       // The instance must be set up and ready for state operations.
       const auto capabilities = instance.model_description().capabilities;
       if (capabilities.can_save_state) {
           const auto saved = instance.save_state();
           // Perform permitted operations, then return to the saved state.
           instance.restore_state(saved);
           // Serialization requires a separate capability.
           if (capabilities.can_export_state) {
               const auto serialized = instance.export_state(saved);
               const auto imported = instance.import_state(serialized);
               instance.restore_state(imported);
               instance.release_state(imported);
           }
           instance.release_state(saved); // Release instance-owned handles.
       }
   }

State handles belong to the instance. Persist the serialized node using the
application's chosen storage if it is needed beyond the current process.
For a paused execution with compatible
participants, ``run.export_current_state()`` and ``run.import_state(state)``
save and restore the coordinated run; its structure must remain unchanged.
See the executable snapshot fragment in :doc:`../../design/infrastructure`.
