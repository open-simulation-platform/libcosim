from typing import Optional

from ._internal import cosim, variable_id, set_last_error
from . import CosimConstants


class EccoParams:
    """Ecco algorithm parameters. Durations in seconds (as in libcosimpy)."""

    safety_factor: float = 0.8
    step_size: float = 0.01
    min_step_size: float = 1e-4
    max_step_size: float = 0.1
    min_change_rate: float = 0.01
    max_change_rate: float = 0.2
    abs_tolerance: float = 1e-4
    rel_tolerance: float = 1e-4
    p_gain: float = 0.2
    i_gain: float = 0.15

    def __init__(self, **kwargs):
        for key, value in kwargs.items():
            if not hasattr(EccoParams, key):
                raise TypeError(f"Unknown ECCO parameter: {key}")
            setattr(self, key, value)


class CosimAlgorithm:
    __create_key: object = object()

    def __init__(self, create_key: object = None, algorithm_ptr=None):
        assert create_key is CosimAlgorithm.__create_key, (
            "Execution can only be initialized using the CosimAlgorithm.create"
        )
        self.__ptr = algorithm_ptr

    @classmethod
    def create_ecco_algorithm(cls, param: EccoParams) -> "CosimAlgorithm":
        c = cosim()
        p = c.ecco_algorithm_params()
        p.safety_factor = float(param.safety_factor)
        p.step_size = c.to_nanoseconds(float(param.step_size))
        p.min_step_size = c.to_nanoseconds(float(param.min_step_size))
        p.max_step_size = c.to_nanoseconds(float(param.max_step_size))
        p.min_change_rate = float(param.min_change_rate)
        p.max_change_rate = float(param.max_change_rate)
        p.abs_tolerance = float(param.abs_tolerance)
        p.rel_tolerance = float(param.rel_tolerance)
        p.p_gain = float(param.p_gain)
        p.i_gain = float(param.i_gain)
        return cls(cls.__create_key, c.ecco_algorithm(p))

    def add_power_bond(
        self,
        slave1_index: int,
        slave1_output_reference: int,
        slave1_input_reference: int,
        slave2_index: int,
        slave2_output_reference: int,
        slave2_input_reference: int,
    ) -> int:
        """
        Creates a power bond between two instances of models.

        Passes the references to libcosim in the same order as libcosimc's
        cosim_ecco_add_power_bond(), which libcosimpy calls.
        """
        real = cosim().variable_type_real
        try:
            self.__ptr.add_power_bond(
                variable_id(slave1_index, real, slave1_output_reference),
                variable_id(slave1_index, real, slave1_input_reference),
                variable_id(slave2_index, real, slave2_output_reference),
                variable_id(slave2_index, real, slave2_input_reference),
            )
            return CosimConstants.success
        except Exception as e:
            set_last_error(e)
            return CosimConstants.failure

    @property
    def ptr(self) -> Optional[object]:
        return self.__ptr
