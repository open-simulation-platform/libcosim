Model and variable metadata
===========================

The model description exposes advertised simulator capabilities and available
variable names, value references, exact types, causality, variability,
initial kinds, starts, dimensions and associated Clocks. It also exposes
declared types, quantities, units and display units, numeric constraints,
Enumeration items, Binary properties and other attributes provided by FMI
Library. FMI 3 nested aliases can be resolved by name and retain their own
description and display unit. Optional metadata can be absent or inapplicable.
``capabilities.can_handle_variable_step`` reflects each imported FMI FMU's
communication-step capability. A false value requires a constant requested
step size when the FMU is used through ``slave_simulator``.

Example
-------

Given an imported ``fmu``, look up a variable before selecting a typed API.
Replace ``"temperature"`` with a name in that FMU:

.. code-block:: cpp

   #include <cosim/fmi/v3/fmu.hpp>
   #include <string>

   void inspect_output(const cosim::fmi::v3::fmu& fmu,
                       const std::string& name)
   {
       const auto description = fmu.model_description();
       const auto variable = cosim::find_variable(*description, name);
       if (variable && variable->type == cosim::variable_type::real &&
           variable->causality == cosim::variable_causality::output) {
           const auto reference = variable->reference;
           const auto declared_dimensions = variable->dimensions;
           // Read this Float64 output using reference and its resolved shape.
       }
   }

Inspect ``declared_type``, ``unit``, ``start`` and other optional fields only
when present. A variable with no dimensions is scalar; dimensions referencing
structural parameters need runtime resolution on an instance.
