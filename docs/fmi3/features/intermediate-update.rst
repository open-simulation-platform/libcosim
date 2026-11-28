Intermediate Update
===================

``co_simulation_callbacks`` registers an Intermediate Update callback and
eligible continuous input and output references. Registered exact typed
scalars and whole arrays can be accessed when the callback flags permit it:
Gets apply to registered outputs, Sets to registered inputs, and Gets must
precede Sets. If ``can_return_early`` is true, the callback may request any
time from the start to the end of the current communication step, inclusive.
For example, a callback at 1.75 s in a step from 1 to 2 s may request 1.5 s.
The request is not the step result: use the FMU's reported endpoint.

Callback times may move backwards only as far as the most recent
finished intermediate step. Callback exceptions make the instance unusable
and are propagated once by the owning instance; subsequent operations on
that instance fail. The importer can still release the instance unless the
FMU itself returns ``fmi3Fatal``.
Automatic event-boundary rewind and replay is disabled for an instance with an
Intermediate Update callback because replay could repeat its side effects.
Explicit FMU save and restore remain available when advertised by the FMU.

Example
-------

Given an imported FMI 3 FMU and the name of an eligible continuous output,
look up its reference before instantiation. Request an early return only if
the FMU permits it:

.. code-block:: cpp

   #include <cosim/fmi/v3/fmu.hpp>
   #include <stdexcept>
   #include <string>

   std::shared_ptr<cosim::fmi::v3::slave_instance> make_with_update(
       cosim::fmi::v3::fmu& fmu, const std::string& output_name)
   {
       // Register an eligible continuous output before instantiation.
       const auto output = cosim::find_variable(*fmu.model_description(), output_name);
       if (!output || !output->intermediate_update ||
           output->causality != cosim::variable_causality::output ||
           output->variability != cosim::variable_variability::continuous) {
           throw std::runtime_error("Expected an Intermediate Update output");
       }
       cosim::fmi::v3::co_simulation_callbacks callbacks;
       callbacks.required_intermediate_variables = {output->reference};
       callbacks.intermediate_update =
           [](cosim::fmi::v3::slave_instance&,
              cosim::fmi::v3::intermediate_update_info& info) {
               // Request a return at the callback time only when permitted.
               if (info.can_return_early) {
                   info.early_return_requested = true;
                   info.early_return_time = info.time;
               }
           };
       return fmu.instantiate_v3_slave("with-update", callbacks);
   }

The reference must be marked for Intermediate Update. To access registered
values in the callback, first check its Get/Set flags and perform all Gets
before Sets. The stepping caller must inspect the early-return result.
For OSP and SSP structures, configure selected instances before injection
using the hook in :doc:`system-structure-configuration`; automatic replay
remains disabled for callback-enabled instances.
