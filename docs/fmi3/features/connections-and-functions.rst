Connections and functions
=========================

Whole-value connections support simulator-to-simulator,
simulator-to-function, function-to-simulator and function-to-function paths.
Endpoints require equal normalized types and compatible resolved shapes;
Enumeration and Integer are distinct connection types. FMI 2 Real and FMI 3
Float64, FMI 2 Integer and FMI 3 Int32, and FMI 2/3 Boolean share their
respective normalized types. Other numeric widths are not implicitly
converted.

Function I/O can declare fixed or parameterized dimensions. Function chains
are evaluated in topological order, with cycles rejected. Structural
parameters and Clocks are not ordinary data connections.

Example
-------

For a not-yet-initialized execution and two already-added simulators, look
up their named Float64 variables and connect them. Their resolved shapes
must match:

.. code-block:: cpp

   #include <cosim/execution.hpp>
   #include <stdexcept>
   #include <string>

   void connect_real(cosim::execution& run, cosim::simulator_index source,
                     const std::string& output_name, cosim::simulator_index target,
                     const std::string& input_name)
   {
       // Both simulators are already added; connect before initialization.
       const auto output = cosim::find_variable(run.get_model_description(source), output_name);
       const auto input = cosim::find_variable(run.get_model_description(target), input_name);
       if (!output || !input || output->type != cosim::variable_type::real ||
           input->type != cosim::variable_type::real ||
           output->causality != cosim::variable_causality::output ||
           input->causality != cosim::variable_causality::input) {
           throw std::runtime_error("Expected compatible Real endpoints");
       }
       // The execution checks compatibility, including resolved array shapes.
       run.connect_variables(
           cosim::variable_id{source, cosim::variable_type::real, output->reference},
           cosim::variable_id{target, cosim::variable_type::real, input->reference});
   }

``source`` and ``target`` are indices returned by ``add_slave``; see
:doc:`../../getting_started` for adding participants. This works for matching
whole arrays as well as scalars. For a function inserted between endpoints,
see the concrete linear-transformation example in
:doc:`../../design/connections`.
