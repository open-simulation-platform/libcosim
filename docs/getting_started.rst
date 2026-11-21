Getting started
===============

libcosim coordinates time-stepped participants such as FMI Co-Simulation
FMUs. A typical run follows these steps:

1. Import an FMU or resolve a model URI and inspect its variable metadata.
2. Choose an algorithm and create an ``execution`` with a start time.
3. Add slave instances, connections, initial values and observers, or inject a
   named ``system_structure`` that describes them.
4. Initialize the execution, advance it, and read the observed values.

An FMU is a model that can create instances; an execution operates on those
instances. Each execution needs its own algorithm. The built-in fixed-step
and ECCO algorithms coordinate supported FMI 1, 2 and 3 Co-Simulation
participants. For the distinction between setup and execution, see
:doc:`architecture`; :doc:`design/workflows` illustrates the FMI 1, 2 and 3
simulation paths.

Build an application
--------------------

Use the repository README to build or obtain libcosim and configure its
dependencies. For a CMake application with libcosim and its dependencies
available as packages, link its exported target:

.. code-block:: cmake

   # The exported libcosim target references libcbor.
   find_package(libcbor CONFIG REQUIRED)
   find_package(libcosim CONFIG REQUIRED)
   add_executable(cosim_example main.cpp)
   target_compile_features(cosim_example PRIVATE cxx_std_17)
   target_link_libraries(cosim_example PRIVATE libcosim::cosim)

The installed libcosim CMake package currently exposes the libcbor target
without finding it automatically, so the example finds libcbor explicitly.
At run time, make the libcosim shared library and its dependencies
discoverable by the platform's loader, for example through ``PATH`` on
Windows or ``LD_LIBRARY_PATH`` on Linux.

Run one FMU
-----------

The following complete ``main.cpp`` imports one non-clocked Co-Simulation
FMU, advances it from 0 to 1 second using a 0.01-second base step, and
prints its last observed scalar Real output. Supply an FMU path and output
variable name on the command line. The selected variable must be an
unclocked scalar Real (FMI 3 Float64) output. For example, the repository's
``tests\data\fmi2\vector.fmu`` has an output named ``output[0]``.

.. code-block:: cpp

   #include <cosim/algorithm/fixed_step_algorithm.hpp>
   #include <cosim/execution.hpp>
   #include <cosim/fmi/fmu.hpp>
   #include <cosim/fmi/importer.hpp>
   #include <cosim/model_description.hpp>
   #include <cosim/observer/last_value_observer.hpp>
   #include <cosim/time.hpp>
   #include <gsl/span>
   #include <exception>
   #include <iostream>
   #include <memory>

   int main(int argc, char* argv[])
   {
       if (argc != 3) {
           std::cerr << "Usage: cosim_example <fmu-path> <output-name>\n";
           return 1;
       }

       try {
           // Import chooses the FMI version; look up the requested output.
           auto importer = cosim::fmi::importer::create();
           auto fmu = importer->import(cosim::filesystem::path(argv[1]));
           const auto description = fmu->model_description();
           const auto output = cosim::find_variable(*description, argv[2]);
           if (!output || output->type != cosim::variable_type::real ||
               output->causality != cosim::variable_causality::output ||
               !output->dimensions.empty() || !output->clocks.empty()) {
               std::cerr << "Expected an unclocked scalar Real output\n";
               return 1;
           }

           // Assemble the execution and subscribe before initialization.
           auto algorithm = std::make_shared<cosim::fixed_step_algorithm>(
               cosim::to_duration(0.01));
           cosim::execution run(cosim::to_time_point(0.0), algorithm);
           auto observer = std::make_shared<cosim::last_value_observer>();
           run.add_observer(observer);
           const auto simulator = run.add_slave(
               fmu->instantiate_slave("model"), "model");

           // A stop or termination may prevent reaching the target.
           run.initialize();
           const bool reached = run.simulate_until(cosim::to_time_point(1.0));
           if (!reached) {
               std::cerr << "Simulation stopped at "
                         << cosim::to_double_time_point(run.current_time())
                         << " seconds\n";
               return 1;
           }

           // Read the latest value recorded by the observer.
           const auto reference = output->reference;
           double value = 0.0;
           observer->get_real(simulator, gsl::make_span(&reference, 1),
                              gsl::make_span(&value, 1));
           std::cout << output->name << " = " << value << '\n';
       } catch (const std::exception& error) {
           std::cerr << "Co-simulation failed: " << error.what() << '\n';
           return 1;
       }
       return 0;
   }

For example, from the repository root on Windows, with the executable on
``PATH``:

.. code-block:: console

   cosim_example.exe tests\data\fmi2\vector.fmu "output[0]"

The importer determines the FMI version and creates the appropriate FMU
implementation. The execution wraps the instance and delegates stepping to
the algorithm. The observer records values at initialization and completed
steps. Always check the result of ``simulate_until``: a stop or termination
request can prevent reaching the requested time. Alternatively, call
``run.step()`` and use its returned *actual* duration and
``run.current_time()`` to advance incrementally.

Connect multiple models
-----------------------

A ``system_structure`` is a reusable description of named models and their
connections. The following fragment uses two instances of the repository's
``vector.fmu``; ``path`` is an absolute path to that FMU. Its
``output[0]`` and ``input[0]`` are compatible Real variables.

.. code-block:: cpp

   #include <cosim/algorithm/fixed_step_algorithm.hpp>
   #include <cosim/execution.hpp>
   #include <cosim/orchestration.hpp>
   #include <cosim/system_structure.hpp>
   #include <cosim/uri.hpp>
   #include <memory>

   bool run_connected(const cosim::filesystem::path& path)
   {
       // Resolve one FMU as a reusable model and connect two instances.
       auto resolver = cosim::default_model_uri_resolver();
       auto model = resolver->lookup_model(cosim::path_to_file_uri(path));
       cosim::system_structure system;
       system.add_entity("source", model);
       system.add_entity("target", model);
       system.connect_variables({"source", "output[0]"},
                                {"target", "input[0]"});

       // Initial values and connections are applied during injection.
       cosim::variable_value_map initial_values;
       cosim::add_variable_value(initial_values, system,
                                 {"source", "input[0]"}, 2.0);
       auto algorithm = std::make_shared<cosim::fixed_step_algorithm>(
           cosim::to_duration(0.01));
       cosim::execution run(cosim::to_time_point(0.0), algorithm);
       const auto ids = cosim::inject_system_structure(run, system, initial_values);
       run.initialize();
       return run.simulate_until(cosim::to_time_point(1.0));
   }

Injection instantiates the models, sets initial values and connects them
before initialization. Inside the function, ``ids.simulators.at("target")``
identifies the target when using an observer or manipulator. Check the
returned result; for the actual time, inspect ``run.current_time()`` before
the execution is destroyed. In a real system, resolve each model
and choose names, types, causalities and shapes from its metadata; a target
input must have a compatible source.

For file-based OSP and SSP setup, see :doc:`design/configuration`. For FMI 3
callbacks, Event Mode, Clocks and other version-specific operations, see
:doc:`fmi3/usage` and :doc:`fmi3/implemented`.

Build this guide
----------------

To build the guide itself, install `uv <https://docs.astral.sh/uv/>`_ and,
for direct CMake builds, Doxygen. From the repository root, run
``uv venv .venv`` if needed, then
``uv pip install --python .venv -r docs/requirements.txt``. Configure CMake
with ``LIBCOSIM_BUILD_DOCS=ON`` and build normally or build the ``doc``
target separately. This includes the public and internal API references.
The generated guide is in the build tree
under ``output/sphinx/index.html``. See ``docs\README.md`` for platform
specific build instructions.
