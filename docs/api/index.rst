API reference
=============

The reference below is generated from Doxygen comments through Breathe.

.. toctree::
   :maxdepth: 1

   internal

Execution and algorithms
------------------------

.. doxygenclass:: cosim::execution
   :members:

.. doxygenclass:: cosim::algorithm
   :members:

.. doxygenclass:: cosim::fixed_step_algorithm
   :members:

.. doxygenclass:: cosim::ecco_algorithm
   :members:

Slaves and model descriptions
-----------------------------

.. doxygenclass:: cosim::slave
   :members:

.. doxygenclass:: cosim::simulator
   :members:

.. doxygenstruct:: cosim::model_description
   :members:

FMI
---

.. doxygenclass:: cosim::fmi::importer
   :members:

.. doxygenclass:: cosim::fmi::fmu
   :members:

.. doxygenclass:: cosim::fmi::v3::fmu
   :members:

.. doxygenclass:: cosim::fmi::v3::slave_instance
   :members:

.. doxygenstruct:: cosim::fmi::v3::co_simulation_callbacks
   :members:

System setup and extension points
---------------------------------

.. doxygenclass:: cosim::system_structure
   :members:

.. doxygenclass:: cosim::model
   :members:

.. doxygenclass:: cosim::model_uri_resolver
   :members:

.. doxygenclass:: cosim::function
   :members:

.. doxygenclass:: cosim::function_type
   :members:

Observers and manipulators
--------------------------

.. doxygenclass:: cosim::observer
   :members:

.. doxygenclass:: cosim::last_value_observer
   :members:

.. doxygenclass:: cosim::time_series_observer
   :members:

.. doxygenclass:: cosim::file_observer
   :members:

.. doxygenclass:: cosim::manipulable
   :members:

.. doxygenclass:: cosim::manipulator
   :members:
