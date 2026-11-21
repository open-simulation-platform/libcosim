Connections and functions
=========================

Connections transfer output values to inputs at communication points chosen
by the algorithm. Types, causalities and resolved whole-value shapes must be
compatible. Functions transform values between participants; built-in
functions include a scalar linear transformation and vector sum. Function
chains are ordered by dependency, and cycles are rejected.

Given a not-yet-initialized execution, two added simulator indices and the
names of matching scalar Float64 variables, retrieve their references and
insert ``out = 1 + 2 * in``:

.. code-block:: cpp

   #include <cosim/execution.hpp>
   #include <cosim/function/linear_transformation.hpp>
   #include <memory>
   #include <stdexcept>
   #include <string>

   void connect_with_scale(cosim::execution& run, cosim::simulator_index source,
                           const std::string& output_name, cosim::simulator_index target,
                           const std::string& input_name)
   {
       // Resolve scalar Real endpoints on the already-added simulators.
       const auto output = cosim::find_variable(run.get_model_description(source), output_name);
       const auto input = cosim::find_variable(run.get_model_description(target), input_name);
       if (!output || !input || output->type != cosim::variable_type::real ||
           input->type != cosim::variable_type::real ||
           output->causality != cosim::variable_causality::output ||
           input->causality != cosim::variable_causality::input ||
           !output->dimensions.empty() || !input->dimensions.empty()) {
           throw std::runtime_error("Expected scalar Real output and input");
       }
       // Route source -> (1 + 2 * input) -> target before initialization.
       const auto scale = run.add_function(
           std::make_shared<cosim::linear_transformation_function>(1.0, 2.0));
       run.connect_variables(
           cosim::variable_id{source, cosim::variable_type::real, output->reference},
           cosim::function_io_id{
               scale, cosim::variable_type::real,
               cosim::linear_transformation_function::in_io_reference});
       run.connect_variables(
           cosim::function_io_id{
               scale, cosim::variable_type::real,
               cosim::linear_transformation_function::out_io_reference},
           cosim::variable_id{target, cosim::variable_type::real, input->reference});
   }

The indices come from ``run.add_slave``; see :doc:`../getting_started`.
An application can instead connect named endpoints on ``system_structure``.
For whole arrays, use compatible whole-value function implementations;
the built-in linear transformation is scalar. FMI 3 Clock connections are
scheduling paths rather than ordinary data values. See
:doc:`../fmi3/features/connections-and-functions` and
:doc:`../fmi3/features/clock-and-data-routing`.
