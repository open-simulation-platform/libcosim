"""libcosimpy-compatible API on top of the SWIG-generated libcosim bindings.

A drop-in replacement for the public modules of libcosimpy (CosimExecution,
CosimObserver, CosimManipulator, CosimSlave, CosimAlgorithm, CosimEnums,
CosimLogging, CosimConstants), implemented in pure Python on the `libcosim`
SWIG module instead of ctypes on libcosimc. It reproduces libcosimpy's
behaviour, including its conventions (times in integer nanoseconds, EccoParams
durations in seconds, libcosimc's numeric error codes, names as bytes).

Purpose: let existing libcosimpy code and tests run unchanged during a
migration to the SWIG bindings.
"""
