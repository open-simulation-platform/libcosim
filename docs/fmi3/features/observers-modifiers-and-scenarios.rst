Observers, modifiers and scenarios
==================================

Last-value and time-series observers expose complete exact scalar or array
values. The file observer writes each whole value in one CSV field, with
bracketed arrays, escaped strings and hexadecimal Binary values.

Whole-value modifiers and overrides support exact scalars and arrays,
including Clock-associated values. Removing a whole-value input modifier
schedules the unmodified cached value for restoration. Ordinary output
modifiers are reused on event refresh when the raw value has not changed.
YAML scenarios support full-value override and reset for fixed-shape values;
unsupported arithmetic and unresolved dynamic shapes are rejected.
In-memory scenarios can also use whole-value callbacks. Their simulator
indices must already be registered; replacing a scenario removes its
previously installed modifiers.

Example
-------

Given a not-yet-initialized execution, an already-added simulator index and
the names of its scalar Float32 input and output, look up their references
and attach a whole-value override and a last-value observer:

.. code-block:: cpp

   #include <cosim/execution.hpp>
   #include <cosim/manipulator/override_manipulator.hpp>
   #include <cosim/observer/last_value_observer.hpp>
   #include <memory>
   #include <stdexcept>
   #include <string>

   void observe_with_override(cosim::execution& run, cosim::simulator_index sim,
                              const std::string& input_name,
                              const std::string& output_name)
   {
       // Resolve scalar Float32 endpoints on the already-added simulator.
       const auto model = run.get_model_description(sim);
       const auto input = cosim::find_variable(model, input_name);
       const auto output = cosim::find_variable(model, output_name);
       if (!input || !output || input->type != cosim::variable_type::float32 ||
           output->type != cosim::variable_type::float32 ||
           input->causality != cosim::variable_causality::input ||
           output->causality != cosim::variable_causality::output ||
           !input->dimensions.empty() || !output->dimensions.empty()) {
           throw std::runtime_error("Expected scalar Float32 input and output");
       }
       // Install both facilities before initialization.
       auto observer = std::make_shared<cosim::last_value_observer>();
       auto modifier = std::make_shared<cosim::override_manipulator>();
       run.add_observer(observer);
       run.add_manipulator(modifier);
       modifier->override_variable(
           sim, cosim::variable_type::float32, input->reference,
           cosim::variable_value(cosim::float32_values{{1.25F}}));
       run.initialize();
       run.step();
       const auto value = observer->get_variable_value(
           sim, cosim::variable_type::float32, output->reference);
       // Remove the override; this does not rewind the run.
       modifier->reset_variable(
           sim, cosim::variable_type::float32, input->reference);
   }

See :doc:`../../getting_started` for creating the execution and simulator.
An array override uses the same API with the matching dimensions. A ``scenario_manager`` can
schedule overrides instead; :doc:`../../design/interaction` includes a
complete in-memory scenario example with an event and end time.
