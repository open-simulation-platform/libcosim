"""Internal helpers of the libcosimpy compatibility layer."""

import libcosim as _cosim

from . import CosimConstants, CosimEnums


def libcosimc():
    """Returns the loaded native library (here: the SWIG module), like libcosimpy."""
    return _cosim


def cosim():
    """The SWIG-generated libcosim module."""
    return _cosim


# CosimVariableType (libcosimc numbering) <-> cosim::variable_type
_VARIABLE_TYPES = {
    CosimEnums.CosimVariableType.REAL.value: _cosim.variable_type_real,
    CosimEnums.CosimVariableType.INTEGER.value: _cosim.variable_type_integer,
    CosimEnums.CosimVariableType.STRING.value: _cosim.variable_type_string_value,
    CosimEnums.CosimVariableType.BOOLEAN.value: _cosim.variable_type_boolean_value,
}
_VARIABLE_TYPES_BACK = {v: k for k, v in _VARIABLE_TYPES.items()}
_VARIABLE_TYPES_BACK[_cosim.variable_type_enumeration] = CosimEnums.CosimVariableType.INTEGER.value

_CAUSALITIES = {
    _cosim.variable_causality_input: CosimEnums.CosimVariableCausality.INPUT.value,
    _cosim.variable_causality_parameter: CosimEnums.CosimVariableCausality.PARAMETER.value,
    _cosim.variable_causality_output: CosimEnums.CosimVariableCausality.OUTPUT.value,
    _cosim.variable_causality_calculated_parameter: CosimEnums.CosimVariableCausality.CALCULATED_PARAMETER.value,
    _cosim.variable_causality_local: CosimEnums.CosimVariableCausality.LOCAL.value,
}

_VARIABILITIES = {
    _cosim.variable_variability_constant: CosimEnums.CosimVariableVariability.CONSTANT.value,
    _cosim.variable_variability_fixed_value: CosimEnums.CosimVariableVariability.FIXED.value,
    _cosim.variable_variability_tunable: CosimEnums.CosimVariableVariability.TUNABLE.value,
    _cosim.variable_variability_discrete: CosimEnums.CosimVariableVariability.DISCRETE.value,
    _cosim.variable_variability_continuous: CosimEnums.CosimVariableVariability.CONTINUOUS.value,
}

# cosim::errc -> libcosimc error code, as cpp_to_c_error_code() in libcosimc.
# Note: libcosimc numbers SIMULATION_ERROR = 11 and ZIP_ERROR = 12, whereas
# libcosimpy's CosimErrorCode enum lacks SIMULATION_ERROR and says
# ZIP_ERROR = 11. The numeric values below are libcosimc's.
_LIBCOSIMC_UNSPECIFIED = 1
_LIBCOSIMC_SIMULATION_ERROR = 11
_LIBCOSIMC_ZIP_ERROR = 12
_ERROR_CODES = {
    _cosim.errc_success: CosimEnums.CosimErrorCode.SUCCESS.value,
    _cosim.errc_bad_file: CosimEnums.CosimErrorCode.BAD_FILE.value,
    _cosim.errc_unsupported_feature: CosimEnums.CosimErrorCode.UNSUPPORTED_FEATURE.value,
    _cosim.errc_dl_load_error: CosimEnums.CosimErrorCode.DL_LOAD_ERROR.value,
    _cosim.errc_model_error: CosimEnums.CosimErrorCode.MODEL_ERROR.value,
    _cosim.errc_simulation_error: _LIBCOSIMC_SIMULATION_ERROR,
    _cosim.errc_zip_error: _LIBCOSIMC_ZIP_ERROR,
}


def to_cosim_variable_type(libcosimc_type: int):
    return _VARIABLE_TYPES[libcosimc_type]


def from_cosim_variable_type(cosim_type) -> int:
    return _VARIABLE_TYPES_BACK[cosim_type]


def from_cosim_causality(c) -> int:
    return _CAUSALITIES[c]


def from_cosim_variability(v) -> int:
    return _VARIABILITIES[v]


def to_libcosimc_error_code(errc) -> int:
    return _ERROR_CODES.get(errc, _LIBCOSIMC_UNSPECIFIED)


def error_code_of(exception) -> int:
    """libcosimc error code for an exception raised by the SWIG module."""
    if isinstance(exception, _cosim.CosimError) and len(exception.args) >= 2:
        return to_libcosimc_error_code(exception.args[1])
    return _LIBCOSIMC_UNSPECIFIED


_last_error_message = ""


def set_last_error(exception) -> None:
    global _last_error_message
    _last_error_message = str(exception.args[0] if exception.args else exception)


def get_last_error_message() -> str:
    return _last_error_message


def variable_id(slave_index: int, cosim_type, reference: int):
    v = _cosim.variable_id()
    v.simulator = slave_index
    v.type = cosim_type
    v.reference = reference
    return v


def to_nanoseconds_int(value, what: str) -> int:
    """libcosimpy accepts ints and int-convertible floats/strings as nanoseconds."""
    try:
        return int(value)
    except TypeError:
        raise TypeError(f"{what} must be an int convertible")
    except ValueError:
        try:
            return int(float(value))
        except ValueError:
            raise ValueError(f"{what} must be an int convertible")
