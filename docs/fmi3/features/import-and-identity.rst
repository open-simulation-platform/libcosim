Import and identity
===================

The existing ``cosim::fmi::importer`` detects FMI 3, extracts or reuses the FMU
cache, and dispatches to ``cosim::fmi::v3::fmu``. FMI 3
``instantiationToken`` is stored separately from FMI 1/2 UUID metadata and
passed to the FMU at instantiation. It is not a cache key: different FMUs
can share a token. The importer identifies packed FMI 3 archives by their
contents and unpacked FMI 3 FMUs by their canonical path. Value references
remain 32-bit.
The default temporary cache protects an extracted directory while any
read-only handle is alive; it is not safe for concurrent access by threads.
Overlapping or reentrant imports on the same importer throw
``std::logic_error``. This check does not make the importer, its FMUs or cache
generally thread-safe.

Co-Simulation FMUs are imported through the generic FMI and slave interfaces
or the FMI 3-specific interface. Model Exchange-only and Scheduled
Execution-only FMUs are rejected; for a dual-interface FMU only
Co-Simulation is used.

Example
-------

For an application-supplied ``path``, check the imported type before using
FMI 3-specific operations:

.. code-block:: cpp

   #include <cosim/fmi/importer.hpp>
   #include <cosim/fmi/v3/fmu.hpp>
   #include <memory>
   #include <stdexcept>

   std::shared_ptr<cosim::fmi::v3::fmu> import_fmi3(
       const cosim::filesystem::path& path)
   {
       // The generic importer detects the version; verify it is FMI 3.
       auto importer = cosim::fmi::importer::create();
       auto fmu = std::dynamic_pointer_cast<cosim::fmi::v3::fmu>(
           importer->import(path));
       if (!fmu) throw std::runtime_error("Expected an FMI 3 FMU");
       const auto token = fmu->model_description()->instantiation_token;
       auto instance = fmu->instantiate_v3_slave("model");
       return fmu;
   }

The token identifies the FMU description; it is not an instance name or an
FMI 1/2 UUID. Use the generic importer result directly when no FMI 3-specific
operation is needed.
