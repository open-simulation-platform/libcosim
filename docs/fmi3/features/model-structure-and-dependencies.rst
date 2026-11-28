ModelStructure and dependencies
===============================

The FMI 3 ``model_structure`` exposes the declared outputs,
continuous-state derivatives, clocked states, initial unknowns and event
indicators, in declaration order, with dependency kinds. Output dependencies
are also available on variable descriptions. An absent dependency list means
unknown/all; an explicitly empty list means no declared algebraic
dependencies. These lists do not describe every state, time or Clock effect.

An FMU advertising ``providesPerElementDependencies`` can be queried through
``get_variable_dependencies()`` for runtime element dependencies. Query
again after a relevant structural-parameter change. Independent entries are
omitted. Fixed-step and ECCO use declared output dependencies to order
direct event paths, while still propagating actual runtime changes.
The importer normalizes supported leading plus signs in ModelStructure
dependency lists before FMI Library parsing, without modifying an unpacked
FMU supplied by the caller.

Example
-------

Given an FMI 3 instance and the name of a variable to query:

.. code-block:: cpp

   #include <cosim/fmi/v3/fmu.hpp>
   #include <stdexcept>
   #include <string>

   void inspect_dependencies(cosim::fmi::v3::slave_instance& instance,
                             const std::string& variable_name)
   {
       const auto model = instance.model_description();
       for (const auto& output : model.structure.outputs) {
           if (!output.dependencies) {
               // Dependencies are unspecified, not an empty dependency list.
               continue;
           }
           for (const auto& dependency : *output.dependencies) {
               const auto known = dependency.variable;
               const auto kind = dependency.kind;
               // Record the declared algebraic dependency.
           }
       }
       const auto variable = cosim::find_variable(model, variable_name);
       if (!variable) throw std::runtime_error("Variable not found");
       if (instance.provides_per_element_dependencies()) {
           const auto dependencies =
               instance.get_variable_dependencies(variable->reference);
       }
   }

The selected variable must support the runtime query. Runtime element
dependencies and declared lists are separate sources of information; query
again after a relevant structural change.
