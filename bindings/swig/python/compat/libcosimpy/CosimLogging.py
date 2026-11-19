from enum import Enum

from ._internal import cosim


class CosimLogLevel(Enum):
    """Enum log levels"""

    TRACE = 0
    DEBUG = 1
    INFO = 2
    WARNING = 3
    ERROR = 4
    FATAL = 5


def log_output_level(log_level: CosimLogLevel):
    """Sets the output log level of libcosim"""
    # cosim::log::level has the same order as CosimLogLevel.
    cosim().set_output_level(log_level.value)
