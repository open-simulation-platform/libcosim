FMU logging
===========

Choose which messages an FMI 3 Co-Simulation instance emits with
``set_debug_logging()``. Separately, the libcosim logger controls which
forwarded messages appear in your application.

Example
-------

With an imported and instantiated FMI 3 ``slave_instance`` named
``instance``, enable only ``logStatusError`` if the FMU declares it:

.. code-block:: cpp

   #include <cosim/log/simple.hpp>
   #include <cosim/fmi/v3/fmu.hpp>
   #include <string_view>

   cosim::log::setup_simple_console_logging();
   cosim::log::set_global_output_level(cosim::log::info);
   const std::string_view categories[] = {"logStatusError"};
   instance.set_debug_logging(false);
   instance.set_debug_logging(true, categories);

An empty category list applies to all categories. Names come from the FMU's
``LogCategories`` in ``modelDescription.xml``. Unknown names are rejected
before the call reaches the FMU. Configure logging outside FMI callbacks,
for example before setup or while execution is paused.

.. note::

   The logger includes the instance, FMI status and category. To see
   successful FMI messages, set the sink's output level to ``trace``.
   Builds with ``LIBCOSIM_NO_FMI_LOGGING`` suppress forwarded FMU messages.
   Neither filter changes the outcome of an FMI operation.
