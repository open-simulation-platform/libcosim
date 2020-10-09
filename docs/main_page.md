\mainpage Introduction

The [libcosim](https://open-simulation-platform.github.io/libcosim) API can
roughly be divided into two parts, corresponding to the main phases of a
co-simulation. These are:

  * The _setup phase_, which is everything that happens before you actually
    run a simulation, such as reading configuration files, loading FMUs,
    setting up the system for simulation, and so on.
  * The _execution phase_, where we run a simulation, solving the model
    equations, logging and/or displaying output, et cetera.

These phases are not distinguished in any particular way in the API. Most
symbols simply live in the top-level `#cosim` namespace, and there is no
specific naming convention to set them apart. Nor are they entirely distinct
in functionality, as several classes and functions are used in both phases.
Nevertheless, the distinction between "setup" and "execution" provides a good
framework for learning and understanding the libcosim API.

## Setting up a simulation

The central class in the setup phase is `cosim::system_structure`. This
contains a representation of the modelled system, consisting of the entities
(simulators and functions) in it and the connections between them. It requires
a description of each simulator or function, which must be given in the form
of a `cosim::model` or `cosim::function_type` object, respectively. At this
stage, the entities themselves are not instantiated, so no simulator/function
code is actually run.

You can build the system structure from scratch, starting with an empty
`cosim::system_structure` object, or you can read it from a file. libcosim
supports two file formats:

  * The [Open Simulation Platform Interface Specification](https://opensimulationplatform.com/specification/)
    (OSP-IS) system structure format, which we consider our "native" format
    and therefore support fully. Use `cosim::load_osp_config()` to read OSP-IS
    system structure files.
  * The [System Structure & Parametrization](https://ssp-standard.org/) (SSP)
    format, for which we have limited support, but which we aim to support
    more fully in the future. Use `cosim::ssp_loader` to read SSP files.

In both of these formats, the models used in the simulation are specified by
URIs that point to FMUs. These URIs need to be converted into `cosim::model`
objects that represent the FMUs in question. This is the job of a
`cosim::model_uri_resolver`, and usually, you should start with the one
created by `cosim::default_model_uri_resolver()`. You can customise it with
support for additional URI schemes if necessary.

## Executing a simulation

The central class in the execution phase is `cosim::execution`. It manages the
entities involved in a single co-simulation run (i.e., an _execution_) and
provides a high-level interface for driving the simulation process.

By far, the easiest way to set up an execution is to first create a
`cosim::system_structure`, as described above, and inject this structure
into an empty `cosim::execution` using `cosim::inject_system_structure()`.
However, it is also possible to build the execution "by hand" by adding
simulators and functions to it in the form of `cosim::async_slave` and
`cosim::function` objects, respectively.

When would you choose which method? Starting with `cosim::system_structure`
has several advantages:

  * `cosim::system_structure` refers to entities by _name_, whereas
    `cosim::execution` refers to them by numeric IDs.
  * A single `cosim::system_structure` can be used to set up multiple
    `cosim::execution` objects. This is useful in many cases, for example if
    you want to simulate the same system with different initial conditions.
  * `cosim::system_structure` only requires entity _descriptions_, whereas
    `cosim::execution` requires actual entity instances. This has practical
    and performance-related implications, in that the latter could entail
    loading of DLLs with model code, and even network communication in the
    case of distributed co-simulations.

However, if you only need to run a single simulation and therefore need to
instantiate all entities exactly once anyway, and you are OK with using
numerical indices to manipulate them (which you must, anyway, once the
execution is up and running), then reaching straight for `cosim::execution`
may be the right thing to do.

### Getting results

To obtain the results of an execution, i.e. the values of various simulator
variables at various times, create an _observer_. More specifically, add a
`cosim::observer` object using `cosim::execution::add_observer()`.
The `cosim::observer` interface is designed to support many use cases, from
file output to real-time visualisation.

libcosim itself supplies three implementations of this interface, namely:

  * `cosim::file_observer`, which logs output to CSV files
  * `cosim::time_series_observer`, which buffers time series in memory
  * `cosim::last_value_observer`, which simply keeps track of the most
    recent variable values.

### Manipulating the system

Sometimes, you need to influence the simulation in some way. This could be to
change parameters, override variable values, or even transform them in more or
less subtle ways. In libcosim, this is done via the `cosim::manipulator`
interface.

`cosim::manipulator` is very similar to `cosim::observer`, but where the
latter merely has a read-only view of the system, a manipulator has much
extended powers.
More precisely, a `cosim::manipulator` can apply _any transformation_ to _any
variable_ in the system.

libcosim supplies two manipulators:

  * `cosim::override_manipulator`, which, as the name suggests, lets you
    override the value of any variable with a value of your choosing.
  * `cosim::scenario_manager`, which manipulates variables according to a
    given _scenario_.

A scenario can be loaded from a
[file](https://open-simulation-platform.github.io/libcosim/scenario), or it
can be specified in code as a `cosim::scenario::scenario` object. In both
cases, it consists of a series of _events_ that occur at predetermined times.
At each event, some variable is modified in some way.

### Changing the algorithm

Under the hood, `cosim::execution` delegates much of its responsibilities to
a _co-simulation algorithm_ (sometimes called _master_ algorithm). These
responsibilities include:

  * Deciding when to step various simulators forward in time, and how far
  * Keeping simulators synchronised in time
  * Routing data between simulators

In libcosim, a co-simulation algorithm must be implemented as a subclass of
the `cosim::algorithm` interface.

There exist several co-simulation algorithms, and libcosim has been designed
to support even the more advanced ones. The library itself provides only one
option: `cosim::fixed_step_algorithm`. For the most part, this is a rather
simple, fixed-step (i.e., non-adaptive) algorithm. However, it has a nice
extra feature in that it allows the use of different step sizes for different
simulators, as long as they're all multiples of the same base step size.

## Customising libcosim

The library was designed with a large number of use cases in mind, but we are
only able to support so many of them out of the box. If there is something you
need to do, and there is no way to do it with libcosim's built-in
functionality, you may be able to achieve it by _extending_ libcosim in
various ways.

Here is a list of some important customisation points. All of these are
interfaces (pure virtual classes) of which you can make your own
implementations. Many have already been mentioned, but we list them again for
completeness and ease of reference:

  * `cosim::algorithm` – to implement alternative co-simulation algorithms
  * `cosim::observer` – to implement your own observers
  * `cosim::manipulator` – to implement your own manipulators
  * `cosim::model`, `cosim::async_slave` and `cosim::slave` – to implement
    simulators that don't necessarily come in the form of FMUs
  * `cosim::function_type` and `cosim::function` – to implement additional
    function types
  * `cosim::model_uri_sub_resolver` – to implement support for additional
    model URI schemes
  * `cosim::file_cache` – to change the way libcosim caches files (such as
    the unpacked contents of FMUs)
