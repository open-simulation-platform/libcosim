Whole arrays and structural parameters
======================================

``variable_value`` owns one complete scalar or array, with its exact type,
resolved dimensions and flat FMI row-major values. A dimension can be a
literal size or reference a scalar ``UInt64`` constant or structural
parameter. Zero-sized arrays are supported. Reference counts and flattened
element counts are validated independently, with overflow and shape checks.
A single numeric, Boolean, or Enumeration start value in the model
description is broadcast across the array.

* **Configuration Mode:** enter from Instantiated to set fixed or tunable
  structural parameters before initialization.
* **Reconfiguration Mode:** enter from Step Mode, not Event Mode. The FMU
  must declare a tunable structural parameter; only tunable structural
  parameters may be set. Exit back to Step Mode.

Neither mode allows FMI typed getters. Read array values after exiting the
mode.

Resolve dimensions again after changing a structural parameter. Whole-array
reads and writes include String, Binary and Enumeration; each array uses a
single value reference.

Example
-------

For an FMU with a writable scalar UInt64 structural parameter and a Float64
input array whose one dimension references that parameter, pass their names
and a newly instantiated FMI 3 instance:

.. code-block:: cpp

   #include <cosim/fmi/v3/fmu.hpp>
   #include <stdexcept>
   #include <string>
   #include <vector>

   void configure_array(cosim::fmi::v3::slave_instance& instance,
                        const std::string& size_name,
                        const std::string& array_name)
   {
       // Start with a new instance and a UInt64-sized Float64 input array.
       const auto model = instance.model_description();
       const auto size = cosim::find_variable(model, size_name);
       const auto input = cosim::find_variable(model, array_name);
       if (!size || !input || size->type != cosim::variable_type::uint64 ||
           size->causality != cosim::variable_causality::structural_parameter ||
           !size->dimensions.empty() ||
           input->type != cosim::variable_type::real ||
           input->causality != cosim::variable_causality::input) {
           throw std::runtime_error("Expected UInt64 size and Float64 input array");
       }
       // Set the structural size before entering Initialization Mode.
       instance.enter_configuration_mode();
       const cosim::value_reference sizes[] = {size->reference};
       const std::uint64_t lengths[] = {3};
       instance.set_uint64_variables(sizes, lengths);
       instance.exit_configuration_mode();
       instance.setup(cosim::to_time_point(0.0), std::nullopt, std::nullopt);
       // Resolve the array shape after configuration; values are flattened.
       const auto shape = instance.resolved_shape(
           cosim::variable_type::real, input->reference);
       if (shape != std::vector<std::uint64_t>{3}) {
           throw std::runtime_error("Expected a three-element array");
       }
       const cosim::typed_value_reference array{
           cosim::variable_type::real, input->reference};
       const cosim::variable_value value(cosim::real_values{{1.0, 2.0, 3.0}},
                                         shape);
       instance.set_variable_values(gsl::make_span(&array, 1),
                                    gsl::make_span(&value, 1));
       const auto current = instance.get_variable_values(gsl::make_span(&array, 1));
       if (current.front().dimensions().size() != 1 ||
           current.front().dimensions()[0] != 3) {
           throw std::runtime_error("Unexpected array shape");
       }
   }

The input must be readable and writable in Initialization Mode.
For a runtime resize, use the same enter/set/exit sequence at a Step Mode
boundary with a **tunable** structural parameter. The direct-instance API
does not renegotiate connected array shapes in a running ``execution``.
