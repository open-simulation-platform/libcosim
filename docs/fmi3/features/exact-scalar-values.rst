Exact scalar values
===================

Typed reads and writes support ``Float32``, ``Float64``, signed and unsigned
8-, 16-, 32- and 64-bit integers, Boolean, String, Binary and Enumeration.
Strings and Binary results are copied from FMU-owned memory. References,
types, access permissions and lifecycle state are checked before FMI calls.
Legacy four-scalar interfaces remain available but reject arrays and
incompatible exact types.

Generic setter batches are prevalidated, including reference types, shapes
and access. An FMU setter can nevertheless fail after earlier writes have
succeeded, so a batch is not transactional.

Example
-------

Pass an FMI 3 instance in a mode where its scalar Float32 input is writable.
The function retrieves both references from the instance's model description.
``input_name`` and ``output_name`` are names in that FMU, not universal names:

.. code-block:: cpp

   #include <cosim/fmi/v3/fmu.hpp>
   #include <stdexcept>
   #include <string>

   float read_after_write(cosim::fmi::v3::slave_instance& instance,
                          const std::string& input_name,
                          const std::string& output_name)
   {
       // Call in a mode where this scalar input is writable.
       const auto model = instance.model_description();
       const auto input = cosim::find_variable(model, input_name);
       const auto output = cosim::find_variable(model, output_name);
       if (!input || !output ||
           input->type != cosim::variable_type::float32 ||
           output->type != cosim::variable_type::float32 ||
           input->causality != cosim::variable_causality::input ||
           output->causality != cosim::variable_causality::output ||
           !input->dimensions.empty() || !output->dimensions.empty()) {
           throw std::runtime_error("Expected scalar Float32 input and output");
       }
       // Exact scalar calls use one value per reference.
       const cosim::value_reference inputs[] = {input->reference};
       const float values[] = {1.25F};
       instance.set_float32_variables(inputs, values);
       const cosim::value_reference outputs[] = {output->reference};
       float result[] = {0.0F};
       instance.get_float32_variables(outputs, result);
       return result[0];
   }

The output must be a readable scalar Float32. Use the corresponding exact
typed methods for other types, or ``get_variable_values`` and
``set_variable_values`` for a generic whole-value API, as illustrated in
:doc:`whole-arrays-and-structural-parameters`. Reference and value spans in
this scalar example both contain one element.
