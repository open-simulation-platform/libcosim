Design
======

libcosim separates a reusable *description* of a system from an *execution*
of its instantiated participants. An application can also construct an
execution directly. The :ref:`software layers figure <fig-software-layers>`
follows the usual call path downward toward a participant; values, results
and callbacks can flow back. Related modules
and the optional Proxy-FMU path are shown alongside, not as calls through
every layer. Select a box to open its guide.
On narrow screens, scroll the figure horizontally to see the full stack.

.. _fig-software-layers:

.. container::

   .. raw:: html

      <figure class="figure-scroll">
        <div class="figure-viewport">
          <object class="stack-figure figure-layers" type="image/svg+xml" data="_static/figures/layer-stack.svg" aria-label="libcosim software layers and simulation call path">
            Software layers: application, system setup, execution and algorithm, simulator adapter, FMI importer, FMI Library, and FMU.
          </object>
        </div>
        <figcaption>Software layers and simulation call path.</figcaption>
      </figure>

The linked guides cover :doc:`design/system-setup`, :doc:`design/configuration`,
:doc:`design/execution`, :doc:`design/models-and-fmi`,
:doc:`design/connections`, :doc:`design/interaction` and
:doc:`design/infrastructure`. :doc:`design/workflows` follows an FMI 1, 2 or
3 run through these layers, and :doc:`fmi3/overview` maps FMI 3 features to
the stack.

An ``execution`` owns one run and delegates coordination to its algorithm;
an algorithm object is used by only one execution. The simulator adapter
caches exposed values, stages inputs and forwards calls to a ``slave``.
The setup description uses names; an execution uses simulator and function
indices. A structure can be reused to create multiple executions.

Simulation time is signed nanosecond-based logical time, not wall-clock
time. Fixed-step workers can run participants concurrently; custom slaves,
callbacks and modifiers must respect their lifetime and thread-safety
requirements. An exception can leave an execution and its participants
invalid; consult the API contract before attempting recovery.

.. toctree::
   :maxdepth: 2

   design/system-setup
   design/configuration
   design/execution
   design/models-and-fmi
   design/connections
   design/interaction
   design/infrastructure
   design/workflows
