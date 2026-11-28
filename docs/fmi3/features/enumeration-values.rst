Enumeration values
==================

Enumeration has a distinct signed ``enumeration_value`` representation;
FMI 3 transport uses ``Int64``. Imported item names, values, descriptions,
starts and bounds are retained where available. Values must match a declared
item and applicable bounds. A scalar Enumeration assignment can also accept
an ``int`` and normalize it; Enumeration arrays require exact Enumeration
values. Connections require the source's permitted values to be accepted by
the target.

FMI Library's type-level Enumeration metadata APIs have narrower or unsigned
limits, so type bounds are derived from validated imported items. This does
not narrow the signed 64-bit runtime transport.

Example
-------

With an FMI 3 instance in a permitted lifecycle mode, look up a named scalar
Enumeration input and set the declared item whose numeric value is 2:

.. code-block:: cpp

   #include <cosim/fmi/v3/fmu.hpp>
   #include <stdexcept>
   #include <string>

   void set_mode(cosim::fmi::v3::slave_instance& instance,
                 const std::string& input_name)
   {
       // Call in a writable mode for an input that declares item value 2.
       const auto input = cosim::find_variable(instance.model_description(), input_name);
       if (!input || input->type != cosim::variable_type::enumeration ||
           input->causality != cosim::variable_causality::input ||
           !input->dimensions.empty()) {
           throw std::runtime_error("Expected a scalar Enumeration input");
       }
       const cosim::value_reference references[] = {input->reference};
       const cosim::enumeration_value values[] = {{2}};
       instance.set_enumeration_variables(references, values);
   }

For an array, construct an ``enumeration_values`` whole value with the
resolved dimensions. Determine permitted items from the FMU's declared type,
rather than assuming every integer is a valid item.
