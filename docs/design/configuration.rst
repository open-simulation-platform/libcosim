OSP and SSP configuration
=========================

The OSP parser loads a system structure, initial values, start/end times and
algorithm configuration. Model URIs are resolved relative to the file by a
``model_uri_resolver``. This example handles the fixed-step variant; a caller
must separately construct ``ecco_algorithm`` when the configuration contains
``ecco_algorithm_params``.

.. code-block:: cpp

   #include <cosim/algorithm/fixed_step_algorithm.hpp>
   #include <cosim/execution.hpp>
   #include <cosim/orchestration.hpp>
   #include <cosim/osp_config_parser.hpp>
   #include <memory>
   #include <stdexcept>
   #include <variant>

   bool run_osp(const cosim::filesystem::path& config_path)
   {
       // Resolve model URIs relative to the OSP configuration.
       auto resolver = cosim::default_model_uri_resolver();
       const auto config = cosim::load_osp_config(config_path, *resolver);
       // This example handles fixed-step configurations only.
       const auto* params = std::get_if<cosim::fixed_step_algorithm_params>(
           &config.algorithm_configuration);
       if (!params) throw std::runtime_error("Expected fixed-step configuration");
       auto algorithm = std::make_shared<cosim::fixed_step_algorithm>(*params);
       cosim::execution run(config.start_time, algorithm);
       const auto ids = cosim::inject_system_structure(
           run, config.system_structure, config.initial_values);
       run.initialize();
       // Use a fallback only when the file has no stop time.
       const auto end = config.end_time.value_or(cosim::to_time_point(1.0));
       return run.simulate_until(end);
   }

Here ``config_path`` is an application-supplied path and ``end`` must be
later than the start time. Inspect the returned result before assuming the
endpoint was reached. The OSP parser uses fixed-step when its algorithm is omitted.

``ssp_loader`` supports a bounded subset of SSP and returns an algorithm and
named parameter sets. For example, with an application-supplied archive or
SSD-directory path:

.. code-block:: cpp

   #include <cosim/algorithm/fixed_step_algorithm.hpp>
   #include <cosim/execution.hpp>
   #include <cosim/ssp/ssp_loader.hpp>
   #include <memory>
   #include <optional>

   bool run_ssp(const cosim::filesystem::path& ssp_path)
   {
       cosim::ssp_loader loader;
       std::optional<cosim::time_point> stop;
       const auto config = loader.load(ssp_path, stop);
       // Supply a fixed-step fallback if the SSP has no algorithm.
       auto algorithm = config.algorithm;
       if (!algorithm) {
           algorithm = std::make_shared<cosim::fixed_step_algorithm>(
               cosim::to_duration(0.01));
       }
       cosim::execution run(config.start_time, algorithm);
       // The empty name selects the default SSP parameter set.
       const auto ids = cosim::inject_system_structure(
           run, config.system_structure, config.parameter_sets.at(""));
       run.initialize();
       return run.simulate_until(stop.value_or(cosim::to_time_point(1.0)));
   }

The empty parameter-set name selects the default set. OSP 0.2.0 and SSP 2.0
can load whole fixed-shape arrays and exact FMI types, including Enumeration
and Binary. The configuration maps contain ``cosim::variable_value`` objects.
Use ``value.values()`` and ``value.dimensions()`` to inspect them, or pass the
maps unchanged to ``inject_system_structure`` as shown above.
Loading a file does not make unsupported FMI interfaces available. The overload
with ``stop`` exposes the optional default
experiment stop time; the original single-argument overload remains available.
Ensure the fallback stop is later than the start and inspect the returned
result.
See :doc:`../fmi3/features/system-structure-configuration`.
