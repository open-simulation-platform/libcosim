from typing import Sequence

from ._internal import cosim, to_cosim_variable_type, set_last_error
from . import CosimConstants, CosimEnums


class CosimManipulator:
    __create_key: object = object()

    def __init__(self, create_key: object = None, manipulator_ptr=None):
        assert create_key is CosimManipulator.__create_key, (
            "Manipulator can only be initialized using the CosimManipulator.create_* methods"
        )
        self.__ptr = manipulator_ptr

    @classmethod
    def create_override(cls):
        return cls(cls.__create_key, cosim().override_manipulator())

    @classmethod
    def create_scenario_manager(cls):
        return cls(cls.__create_key, cosim().scenario_manager())

    def is_scenario_running(self):
        return self.__ptr.is_scenario_running()

    def abort_scenario(self):
        try:
            self.__ptr.abort_scenario()
            return True
        except Exception as e:
            set_last_error(e)
            return False

    def __slave_values(self, override, slave_index, variable_references, values):
        try:
            for reference, value in zip(variable_references, values):
                override(slave_index, reference, value)
            return True
        except Exception as e:
            set_last_error(e)
            return False

    def slave_real_values(self, slave_index: int, variable_references: list[int], values: Sequence[float]):
        return self.__slave_values(self.__ptr.override_real_variable, slave_index, variable_references,
                                   [float(v) for v in values])

    def slave_integer_values(self, slave_index: int, variable_references: list[int], values: Sequence[int]):
        return self.__slave_values(self.__ptr.override_integer_variable, slave_index, variable_references,
                                   [int(v) for v in values])

    def slave_boolean_values(self, slave_index: int, variable_references: list[int], values: Sequence[bool]):
        return self.__slave_values(self.__ptr.override_boolean_variable, slave_index, variable_references,
                                   [bool(v) for v in values])

    def slave_string_values(self, slave_index: int, variable_references: list[int], values: Sequence[str]):
        return self.__slave_values(self.__ptr.override_string_variable, slave_index, variable_references,
                                   [v.decode() if isinstance(v, bytes) else str(v) for v in values])

    def reset_variables(self, slave_index: int, variable_type: CosimEnums.CosimVariableType,
                        variable_references: list[int]):
        try:
            for reference in variable_references:
                self.__ptr.reset_variable(slave_index, to_cosim_variable_type(variable_type.value), reference)
            return True
        except Exception as e:
            set_last_error(e)
            return False

    def ptr(self):
        return self.__ptr
