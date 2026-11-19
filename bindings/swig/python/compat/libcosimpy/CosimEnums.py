from enum import Enum


class CosimVariableCausality(Enum):
    """Enum for variable causality"""

    INPUT = 0
    PARAMETER = 1
    OUTPUT = 2
    CALCULATED_PARAMETER = 3
    LOCAL = 4
    INDEPENDENT = 5


class CosimVariableVariability(Enum):
    """Enum for variable variability"""

    CONSTANT = 0
    FIXED = 1
    TUNABLE = 2
    DISCRETE = 3
    CONTINUOUS = 4


class CosimVariableType(Enum):
    """Enum for variable type"""

    REAL = 0
    INTEGER = 1
    STRING = 2
    BOOLEAN = 3


class CosimErrorCode(Enum):
    """Enum for library error codes (as in libcosimpy)"""

    NONE = -1
    SUCCESS = 0
    UNSPECIFIED = 1
    ERRNO = 2
    INVALID_ARGUMENT = 3
    ILLEGAL_STATE = 4
    OUT_OF_RANGE = 5
    STEP_TOO_LONG = 6
    BAD_FILE = 7
    UNSUPPORTED_FEATURE = 8
    DL_LOAD_ERROR = 9
    MODEL_ERROR = 10
    ZIP_ERROR = 11


class CosimExecutionState(Enum):
    """Enum for execution simulation states"""

    STOPPED = 0
    RUNNING = 1
    ERROR = 2
