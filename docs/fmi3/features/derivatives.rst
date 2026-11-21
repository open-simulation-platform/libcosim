Derivatives
===========

Capability-gated Co-Simulation APIs provide output, directional and adjoint
derivatives. Output derivatives require continuous floating-point outputs
and an order within the FMU's advertised maximum. Directional and adjoint
queries accept eligible ``Float32`` and ``Float64`` scalars and whole arrays;
the allowed unknown and known categories depend on the lifecycle mode and
ModelStructure.
Declared dependency lists are not request allowlists; a missing dependency
can represent a zero partial derivative.

Directional seeds contain flattened known elements and sensitivities contain
flattened unknown elements; adjoint queries reverse those counts. The
reference and element counts are checked independently, including
zero-element arrays. Directional and adjoint results are published to caller
buffers only after OK or Warning.

Example
-------

For an FMI 3 instance in a permitted mode, retrieve eligible scalar Float64
input and output references by their names:

.. code-block:: cpp

   #include <cosim/fmi/v3/fmu.hpp>
   #include <stdexcept>
   #include <string>

   double directional_sensitivity(cosim::fmi::v3::slave_instance& instance,
                                  const std::string& input_name,
                                  const std::string& output_name)
   {
       // Use eligible scalar Float64 variables in a permitted lifecycle mode.
       const auto model = instance.model_description();
       const auto input = cosim::find_variable(model, input_name);
       const auto output = cosim::find_variable(model, output_name);
       if (!input || !output || input->type != cosim::variable_type::real ||
           output->type != cosim::variable_type::real ||
           input->causality != cosim::variable_causality::input ||
           output->causality != cosim::variable_causality::output ||
           !input->dimensions.empty() || !output->dimensions.empty() ||
           !instance.provides_directional_derivatives()) {
           throw std::runtime_error("Expected derivative-capable scalar Float64 variables");
       }
       const cosim::value_reference unknowns[] = {output->reference};
       const cosim::value_reference knowns[] = {input->reference};
       const double seed[] = {1.0}; // One known element, with a unit seed.
       double sensitivity[] = {0.0};
       instance.get_directional_derivative(unknowns, knowns, seed, sensitivity);
       return sensitivity[0];
   }

The return value is the output's directional sensitivity to the
input for unit seed. For arrays allocate the seed and result for their
flattened element counts, not merely the number of references.

For the same eligible scalar input and a continuous Float64 output, the
following uses an adjoint unit seed and requests a first output derivative
only when the corresponding capability is advertised. The references and
current lifecycle mode must be valid for each request:

.. code-block:: cpp

   #include <cosim/fmi/v3/fmu.hpp>
   #include <cstdint>
   #include <optional>

   struct optional_derivatives {
       std::optional<double> adjoint;
       std::optional<double> output_rate;
   };

   optional_derivatives query_optional_derivatives(
       cosim::fmi::v3::slave_instance& instance,
       cosim::value_reference input, cosim::value_reference output)
   {
       // input and output are eligible scalar references in the current mode.
       optional_derivatives result;
       const cosim::value_reference unknowns[] = {output};
       const cosim::value_reference knowns[] = {input};
       // Each operation requires its own advertised capability.
       if (instance.provides_adjoint_derivatives()) {
           const double seed[] = {1.0};
           double sensitivity[] = {0.0};
           instance.get_adjoint_derivative(unknowns, knowns, seed, sensitivity);
           result.adjoint = sensitivity[0];
       }
       if (instance.max_output_derivative_order() >= 1) {
           const std::int32_t orders[] = {1};
           double values[] = {0.0};
           instance.get_output_derivatives(unknowns, orders, values);
           result.output_rate = values[0];
       }
       return result;
   }
