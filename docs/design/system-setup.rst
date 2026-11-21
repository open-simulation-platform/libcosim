System setup and orchestration
==============================

``system_structure`` describes named simulator models, function types,
connections and optional ECCO power bonds without starting a run. A
``model`` is a blueprint; ``model_uri_resolver`` resolves URIs into models.
The default resolver supports built-in URI schemes including local FMUs.
Validation checks names, causalities and compatible types as the structure
is assembled. The same description can be injected into multiple separate
executions, each with its own algorithm and instances.

The following describes two instances of the same model. The actual FMU must
provide compatible ``output`` and ``input`` variables; the names are examples.
``path`` is an application-supplied absolute FMU path.

.. code-block:: cpp

   #include <cosim/algorithm/fixed_step_algorithm.hpp>
   #include <cosim/execution.hpp>
   #include <cosim/orchestration.hpp>
   #include <cosim/system_structure.hpp>
   #include <cosim/uri.hpp>
   #include <memory>

   bool run_two_models(const cosim::filesystem::path& path)
   {
       // Resolve one FMU as a model and describe two connected instances.
       auto resolver = cosim::default_model_uri_resolver();
       auto model = resolver->lookup_model(cosim::path_to_file_uri(path));
       cosim::system_structure system;
       system.add_entity("source", model);
       system.add_entity("target", model);
       system.connect_variables({"source", "output"}, {"target", "input"});

       auto algorithm = std::make_shared<cosim::fixed_step_algorithm>(
           cosim::to_duration(0.01));
       cosim::execution run(cosim::to_time_point(0.0), algorithm);
       // Injection instantiates and connects the named entities.
       const auto ids = cosim::inject_system_structure(run, system, {});
       const auto source_index = ids.simulators.at("source");
       run.initialize();
       const auto reached = run.simulate_until(cosim::to_time_point(1.0));
       const auto actual_time = run.current_time();
       return reached;
   }

Inside the function, ``ids.simulators.at("source")`` gives the execution-local
index for observation or modification. Check the returned result, and inspect
``run.current_time()`` before the execution is destroyed:
termination or a stop request may end a run before the requested time.
Initial values can be supplied in the third injection argument via
``variable_value_map`` and ``add_variable_value``. For file-based setup see
:doc:`configuration`; for direct assembly see :doc:`execution`.

To customize instantiation before a slave is added, pass a
``model_instantiator`` as the fourth argument. A resolver's FMU-backed model
can be identified safely using ``fmu_backed_model``; custom models remain
usable via their normal ``instantiate`` method. For example, assuming a
structure with an FMI 3 ``source`` supporting Intermediate Update:

.. code-block:: cpp

   #include <cosim/execution.hpp>
   #include <cosim/fmi/v3/fmu.hpp>
   #include <cosim/orchestration.hpp>
   #include <stdexcept>
   #include <utility>

   void inject_with_callback(cosim::execution& run, const cosim::system_structure& system)
   {
       cosim::model_instantiator instantiate =
           [](const std::shared_ptr<cosim::model>& model, std::string_view name)
               -> std::shared_ptr<cosim::slave> {
           // Customize only the selected FMI 3 entity.
           if (name != "source") return model->instantiate(name);
           auto backed = std::dynamic_pointer_cast<cosim::fmu_backed_model>(model);
           auto fmu = backed
               ? std::dynamic_pointer_cast<cosim::fmi::v3::fmu>(backed->fmu())
               : nullptr;
           if (!fmu) throw std::invalid_argument("source must be an FMI 3 FMU");
           cosim::fmi::v3::co_simulation_callbacks options;
           options.intermediate_update =
               [](cosim::fmi::v3::slave_instance&,
                  cosim::fmi::v3::intermediate_update_info&) {};
           return fmu->instantiate_v3_slave(name, std::move(options));
       };
       const auto ids_with_options =
           cosim::inject_system_structure(run, system, {}, instantiate);
   }

Use the hook on a fresh execution instead of the earlier injection. Validate
any required entity names before injection; the hook runs for simulator
entities only. As with ordinary injection, a failure can leave the execution
partially modified. FMI 3 callback restrictions are described in
:doc:`../fmi3/features/intermediate-update`.
