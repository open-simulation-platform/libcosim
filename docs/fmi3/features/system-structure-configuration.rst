System-structure configuration
==============================

OSP ``0.1`` and SSP ``1.0`` files keep their existing scalar syntax. For
exact FMI types and fixed-shape arrays, use OSP ``0.2.0`` or SSP ``2.0``.
Both populate maps of ``cosim::variable_value``; use the scalar convenience
overload of ``add_variable_value`` for programmatic scalar assignments.

OSP 0.2.0
---------

Select ``version="0.2.0"`` on ``OspSystemStructure`` without changing its
namespace. The libcosim schema is ``OspSystemStructure-0.2.0.xsd``.
A scalar uses a ``value`` attribute. For example, a two-element
``UInt64`` array uses whitespace-separated values in FMI row-major order:

.. code-block:: xml

   <InitialValue variable="weights">
       <UInt64 value="42 18446744073709551615"/>
       <Dimension size="2"/>
   </InitialValue>

Use one ``Value`` child per element for String, Binary and Enumeration
arrays. Binary uses hex; OSP Enumeration uses the numeric FMI item value.
Invalid OSP initial values stop loading. Omitting ``Algorithm`` selects
``fixedStep``.

SSP 2.0
-------

The loader supports flat FMU systems, inline or external SSV parameter
sets, and fixed-size ``ssc:Dimension`` declarations. A declared connector
type and shape must agree with the FMU. Clocks connect triggered output to
triggered input; any supplied Clock attributes must agree with the FMU.
SSV Enumeration values use the target FMU's item names. Unusable parameter
assignments are logged and skipped. Variable-size ``sizeConnector`` dimensions
and mapping transformations are unsupported. Linear transformations support
scalar Real (Float64) only.

XML-defined OSP Clocks
----------------------

Place ``Clocks`` after ``Simulators``. Connect the Clock source's ``out``
endpoint to a triggered input on a Co-Simulation FMU with Event Mode:

.. code-block:: xml

   <Clocks>
       <Clock name="driver" intervalVariability="constant"
              supportsFraction="true" intervalCounter="2"
              resolution="4" shiftCounter="1"/>
   </Clocks>
   <Connections>
       <VariableConnection>
           <Variable simulator="driver" name="out"/>
           <Variable simulator="target" name="inClock2"/>
       </VariableConnection>
   </Connections>

This Clock first fires 0.25 s after the start, then every 0.5 s. A decimal
schedule uses ``intervalDecimal`` and optional ``shiftDecimal`` instead.
``fixed`` and ``tunable`` schedules accept an XML-supplied initial interval;
there is no runtime API for retuning a standalone XML Clock. A ``triggered``
source has no interval and relays events received on its ``in`` endpoint.
The OSP schema rejects ``changing`` and ``countdown`` for standalone Clock
sources: those intervals must be supplied at runtime by an FMU. Fractional
intervals must be at least one simulation tick (1 ns), checked using the
exact counter and resolution. Standalone Clock sources
do not support saving simulator state. They do not enable Scheduled Execution.

Example
-------

Load an OSP file containing FMI 3 FMUs and inject its named structure into
an execution whose algorithm was constructed from its configuration.
See :doc:`../../design/configuration` for complete OSP and SSP setup examples,
including the configuration path, and :doc:`../../design/system-setup` for programmatic
structures and a per-entity instantiation hook. The hook can use
``fmu_backed_model::fmu()`` and ``instantiate_v3_slave()`` to supply
Intermediate Update options before injection; unconfigured models can use
their ordinary instantiation path.
