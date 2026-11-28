Models, slaves and FMI
======================

``model`` is an instantiation blueprint. ``slave`` is the runtime interface
for one time-stepped participant; custom slaves need not be FMUs. An
execution wraps each slave in a simulator adapter that stages inputs, caches
exposed values and applies modifiers. Algorithms work with the simulator
interface; application code normally uses ``execution`` or the slave.
When built with ``LIBCOSIM_WITH_PROXYFMU``, the optional Proxy-FMU client
provides a separate remote model and slave path using the legacy scalar
interface. It does not go through the local FMI 3 importer.

The FMI importer probes the version, unpacks or reuses the FMU cache and
creates the appropriate FMI 1, 2 or 3 implementation. Its generic API is
useful when the version is not known in advance. For an application-supplied
FMU path:

.. code-block:: cpp

   #include <cosim/fmi/importer.hpp>
   #include <cosim/fmi/fmu.hpp>

   std::shared_ptr<cosim::slave> import_slave(const cosim::filesystem::path& path)
   {
       // Import selects the FMI version; instantiate creates a new slave.
       auto importer = cosim::fmi::importer::create();
       auto fmu = importer->import(path);
       auto description = fmu->model_description();
       return fmu->instantiate_slave("participant");
   }

``description`` is model metadata; the returned slave is an independent instance.
An instance keeps its FMU alive. Use the FMI 3-specific interface when
capability queries, callbacks, Clocks or derivatives are needed; see
:doc:`../fmi3/usage`. Direct callers must respect the FMU's lifecycle and
step results. An execution's built-in algorithm coordinates these operations
for connected participants.

For FMI 3 archives, cache reuse is based on the archive contents, not its
instantiation token. Unpacked FMI 3 imports use the canonical directory path.
Direct FMI 3 instances only negotiate early return when the caller opts in;
the generic instantiation path opts in for event-aware execution.

The default temporary file cache is single-threaded. Multiple read-only
handles can coexist, and each keeps its directory protected from a writer
until that handle is released. Overlapping or reentrant ``import`` and
``import_unpacked`` calls on one importer throw ``std::logic_error``. This
check does not make the importer, its FMUs or cache generally thread-safe.

To import an existing unpacked FMU directory instead of an archive, supply a
directory containing its model description and binaries:

.. code-block:: cpp

   #include <cosim/fmi/importer.hpp>

   std::shared_ptr<cosim::fmi::fmu> import_directory(
       cosim::fmi::importer& importer, const cosim::filesystem::path& directory)
   {
       // Keep the unpacked directory available for the FMU's lifetime.
       return importer.import_unpacked(directory);
   }

Keep the directory available while the imported FMU uses it.
For example, with a ``cache`` owned by the application:

.. code-block:: cpp

   #include <cosim/file_cache.hpp>

   cosim::temporary_file_cache cache;
   auto writer = cache.get_directory_rw("entry");
   // Populate writer->path() before granting read-only access.
   writer.reset();
   auto first = cache.get_directory_ro("entry");
   auto second = cache.get_directory_ro("entry");
   // Both handles must be released before requesting read/write access.
