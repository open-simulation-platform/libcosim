"""
A thin Pythonic layer on the generated libcosim bindings.

Adds conveniences to the SWIG-generated classes, without changing anything
they already provide (the generated API stays the same in all languages):

- simulators and variables by name:
    ``exe.simulator_index("Crane")``, ``exe.variables("Crane")``,
    ``exe.variable("Crane", "angle")`` (a ``variable_id``);
- one method per operation instead of one per variable type, dispatching on
  the ``variable_id``'s type:
    ``exe.set_initial_value(var, 1.0)``, ``observer.value(var)``,
    ``manipulator.override(var, 1.0)``, ``manipulator.reset(var)``,
    ``time_series.samples(var, from_step, count)``;
- ``with exe.running(): ...`` starts the simulation in the background and
  stops it (and waits until it has stopped) at the end of the block;
- readable ``repr``s with enumerator names.

Times stay ``int`` nanoseconds; use ``to_nanoseconds()`` and ``to_seconds()``.
"""

import contextlib
import time

from . import cosim as _c

_ENUM_PREFIXES = ("variable_type_", "variable_causality_", "variable_variability_", "execution_state_")


def _enum_names(prefix):
    return {getattr(_c, k): k[len(prefix):] for k in dir(_c) if k.startswith(prefix)}


_TYPE_NAMES = _enum_names("variable_type_")
_CAUSALITY_NAMES = _enum_names("variable_causality_")
_VARIABILITY_NAMES = _enum_names("variable_variability_")
_STATE_NAMES = _enum_names("execution_state_")

_REAL = _c.variable_type_real
_INTEGER = _c.variable_type_integer
_BOOLEAN = _c.variable_type_boolean_value
_STRING = _c.variable_type_string_value


def _type_suffix(variable_type):
    """The type part of the generated method names (get_real, ...)."""
    try:
        return {_REAL: "real", _INTEGER: "integer", _BOOLEAN: "boolean", _STRING: "string"}[variable_type]
    except KeyError:
        raise TypeError(f"unsupported variable type: {_TYPE_NAMES.get(variable_type, variable_type)}") from None


def _add(cls, name):
    """Decorator: adds a function to a generated class, never replacing one."""
    def decorate(func):
        if name != "__repr__" and hasattr(cls, name):
            raise RuntimeError(f"libcosim: {cls.__name__}.{name} is already defined by the bindings")
        setattr(cls, name, func)
        return func
    return decorate


# --- execution: names -------------------------------------------------------

@_add(_c.execution, "simulator_index")
def simulator_index(self, name):
    """The index of the simulator with the given name (KeyError if none)."""
    for s in self.get_simulators():
        if s.name == name:
            return s.index
    raise KeyError(f"no simulator named {name!r}")


def _index(exe, simulator):
    return exe.simulator_index(simulator) if isinstance(simulator, str) else simulator


@_add(_c.execution, "variables")
def variables(self, simulator):
    """The variables of a simulator (index or name), as a dict by name."""
    return {v.name: v for v in self.get_model_description(_index(self, simulator)).variables}


@_add(_c.execution, "variable")
def variable(self, simulator, name):
    """A ``variable_id`` for a simulator (index or name) and variable name."""
    index = _index(self, simulator)
    try:
        v = self.variables(index)[name]
    except KeyError:
        raise KeyError(f"simulator {simulator!r} has no variable {name!r}") from None
    return _make_variable_id(index, v.type, v.reference)


def _make_variable_id(simulator, variable_type, reference):
    vid = _c.variable_id()
    vid.simulator, vid.type, vid.reference = simulator, variable_type, reference
    return vid


# --- execution: values and running ------------------------------------------

@_add(_c.execution, "set_initial_value")
def set_initial_value(self, variable, value):
    """Sets the initial value of a variable (``variable_id``), of any type."""
    getattr(self, f"set_{_type_suffix(variable.type)}_initial_value")(
        variable.simulator, variable.reference, value)


@_add(_c.execution, "running")
@contextlib.contextmanager
def running(self, poll_interval=0.001):
    """
    Runs the simulation in the background for the duration of a ``with``
    block: starts it, and at the end stops it and waits until it has
    stopped. Raises ``CosimError`` if the simulation failed.
    """
    self.start_simulation()
    try:
        yield self
    finally:
        if self.get_status().state == _c.execution_state_running:
            self.stop_simulation()
        while self.get_status().state == _c.execution_state_running:
            time.sleep(poll_interval)
    status = self.get_status()
    if status.state == _c.execution_state_error:
        from ._cosim import CosimError
        raise CosimError(status.error_message, status.error_code)


# --- observers and manipulators ---------------------------------------------

@_add(_c.last_value_observer, "value")
def value(self, variable):
    """The last value of a variable (``variable_id``), of any type."""
    get = getattr(self, f"get_{_type_suffix(variable.type)}")
    return get(variable.simulator, [variable.reference])[0]


@_add(_c.time_series_observer, "samples")
def samples(self, variable, from_step, max_samples):
    """Samples of a real or integer variable (``variable_id``); see get_real_samples."""
    if variable.type not in (_REAL, _INTEGER):
        raise TypeError("time series are only recorded for real and integer variables")
    get = getattr(self, f"get_{_type_suffix(variable.type)}_samples")
    return get(variable.simulator, variable.reference, from_step, max_samples)


@_add(_c.override_manipulator, "override")
def override(self, variable, value):
    """Overrides a variable (``variable_id``), of any type, with a value."""
    getattr(self, f"override_{_type_suffix(variable.type)}_variable")(
        variable.simulator, variable.reference, value)


@_add(_c.override_manipulator, "reset")
def reset(self, variable):
    """Removes an override of a variable (``variable_id``)."""
    self.reset_variable(variable.simulator, variable.type, variable.reference)


# --- repr --------------------------------------------------------------------

@_add(_c.variable_id, "__repr__")
def _variable_id_repr(self):
    return (f"variable_id(simulator={self.simulator}, "
            f"type={_TYPE_NAMES.get(self.type, self.type)}, reference={self.reference})")


@_add(_c.variable_description, "__repr__")
def _variable_description_repr(self):
    return (f"variable_description(name={self.name!r}, reference={self.reference}, "
            f"type={_TYPE_NAMES.get(self.type, self.type)}, "
            f"causality={_CAUSALITY_NAMES.get(self.causality, self.causality)}, "
            f"variability={_VARIABILITY_NAMES.get(self.variability, self.variability)})")


@_add(_c.simulator_info, "__repr__")
def _simulator_info_repr(self):
    return f"simulator_info(name={self.name!r}, index={self.index})"


@_add(_c.model_description, "__repr__")
def _model_description_repr(self):
    return (f"model_description(name={self.name!r}, version={self.version!r}, "
            f"variables={len(self.variables)})")


@_add(_c.execution_status, "__repr__")
def _execution_status_repr(self):
    text = (f"execution_status(state={_STATE_NAMES.get(self.state, self.state)}, "
            f"current_time={self.current_time}")
    if self.state == _c.execution_state_error:
        text += f", error_code={self.error_code}, error_message={self.error_message!r}"
    return text + ")"
