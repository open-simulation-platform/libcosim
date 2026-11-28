Observers, manipulators and scenarios
=====================================

Observers receive execution notifications and expose results. The
``last_value_observer`` provides the latest value, ``time_series_observer``
buffers selected variables, and ``file_observer`` writes CSV. Manipulators
influence values; ``override_manipulator`` applies overrides, while
``scenario_manager`` schedules actions relative to a start time.
File-observer decimation factors must be positive; a zero factor is rejected
when the configuration is created or parsed.

For an execution ``run`` and an already-added simulator index ``sim``,
the named input and output must identify suitable scalar Float64 variables.
Add the observer and manipulator before initializing the run:

.. code-block:: cpp

   #include <cosim/execution.hpp>
   #include <cosim/manipulator/override_manipulator.hpp>
   #include <cosim/observer/last_value_observer.hpp>
   #include <memory>
   #include <stdexcept>
   #include <string>

   void observe_override(cosim::execution& run, cosim::simulator_index sim,
                         const std::string& input_name, const std::string& output_name)
   {
       // Resolve an uninitialized simulator's scalar Real input and output.
       const auto model = run.get_model_description(sim);
       const auto input = cosim::find_variable(model, input_name);
       const auto output = cosim::find_variable(model, output_name);
       if (!input || !output || input->type != cosim::variable_type::real ||
           output->type != cosim::variable_type::real ||
           input->causality != cosim::variable_causality::input ||
           output->causality != cosim::variable_causality::output ||
           !input->dimensions.empty() || !output->dimensions.empty()) {
           throw std::runtime_error("Expected scalar Real input and output");
       }
       // Install the observer and override before initializing the run.
       auto observer = std::make_shared<cosim::last_value_observer>();
       auto override = std::make_shared<cosim::override_manipulator>();
       run.add_observer(observer);
       run.add_manipulator(override);
       override->override_real_variable(sim, input->reference, 2.0);
       run.initialize();
       run.step();
       const auto value = observer->get_variable_value(
           sim, cosim::variable_type::real, output->reference);
       // Remove the override without rewinding the simulation.
       override->reset_variable(sim, cosim::variable_type::real, input->reference);
   }

The exact whole-value methods support arrays and additional FMI 3 types.
Resetting an override restores normal value handling; it does not rewind the
simulation. Custom observers and manipulators must respect
the execution's lifetime and concurrency requirements. See
:doc:`../fmi3/features/observers-modifiers-and-scenarios`.

For a time series, register the observer and select the scalar Real output
before initializing the execution:

.. code-block:: cpp

   #include <cosim/execution.hpp>
   #include <cosim/observer/time_series_observer.hpp>
   #include <memory>
   #include <vector>

   std::vector<double> record_one_step(cosim::execution& run,
                                       cosim::simulator_index sim,
                                       cosim::value_reference output)
   {
       // Select a scalar Real output before initialization.
       auto series = std::make_shared<cosim::time_series_observer>(4);
       run.add_observer(series);
       series->start_observing({sim, cosim::variable_type::real, output});
       run.initialize();
       run.step();
       double values[4]{};
       cosim::step_number steps[4]{};
       cosim::time_point times[4]{};
       // Only the first count elements contain recorded samples.
       const auto count = series->get_real_samples(sim, output, 0,
                                                    values, steps, times);
       return {values, values + count};
   }

To schedule an input override for one second after the current time, pass an
already-added simulator index and the name of a compatible Float64 input:

.. code-block:: cpp

   #include <cosim/execution.hpp>
   #include <cosim/manipulator/scenario_manager.hpp>
   #include <cosim/scenario.hpp>
   #include <memory>
   #include <stdexcept>
   #include <string>

   void schedule_override(cosim::execution& run, cosim::simulator_index sim,
                          const std::string& input_name)
   {
       // sim must already be registered; choose a scalar Real input.
       const auto input = cosim::find_variable(run.get_model_description(sim), input_name);
       if (!input || input->type != cosim::variable_type::real ||
           input->causality != cosim::variable_causality::input ||
           !input->dimensions.empty()) {
           throw std::runtime_error("Expected a scalar Real input");
       }
       auto manager = std::make_shared<cosim::scenario_manager>();
       run.add_manipulator(manager);
       // Event and end times are relative to the scenario's load time.
       cosim::scenario::scenario scenario;
       scenario.events.push_back({
           cosim::to_time_point(1.0),
           cosim::scenario::variable_action{
               sim, input->reference,
               cosim::scenario::real_modifier{
                   [](double, cosim::duration) { return 2.0; }},
               true}});
       scenario.end = cosim::to_time_point(3.0);
       manager->load_scenario(scenario, run.current_time());
   }

The event time and end time are relative to the time passed to
``load_scenario``. The action is applied by the manager during execution;
the example's end time ends the scenario, not the co-simulation. All
referenced simulators must already be registered with the manager. Loading
another scenario removes modifiers installed by the previous scenario.
