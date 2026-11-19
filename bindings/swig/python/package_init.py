"""libcosim Python bindings (SWIG spike)."""

import os as _os
import sys as _sys

if _sys.platform == "win32":
    # Since Python 3.8, Windows does not use PATH to find the DLLs an
    # extension module depends on. Register the package directory and any
    # directories listed in LIBCOSIM_DLL_DIRS (used for development builds,
    # where cosim.dll is not next to the module).
    for _dir in [_os.path.dirname(__file__)] + _os.environ.get("LIBCOSIM_DLL_DIRS", "").split(_os.pathsep):
        if _dir and _os.path.isdir(_dir):
            _os.add_dll_directory(_dir)

from .cosim import *  # noqa: E402,F401,F403
from ._cosim import CosimError  # noqa: E402,F401
