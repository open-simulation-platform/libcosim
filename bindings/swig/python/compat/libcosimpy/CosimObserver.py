from typing import Optional

from ._internal import cosim, to_cosim_variable_type, variable_id
from . import CosimEnums


class CosimObserver:
    __create_key: object = object()

    def __init__(self, create_key: object = None, observer_ptr=None):
        assert create_key is CosimObserver.__create_key, (
            "Observer can only be initialized using the CosimObserver.create_* methods"
        )
        self.__ptr = observer_ptr

    @classmethod
    def create_last_value(cls):
        return cls(cls.__create_key, cosim().last_value_observer())

    @classmethod
    def create_to_dir(cls, log_dir: str):
        return cls(cls.__create_key, cosim().file_observer(log_dir))

    @classmethod
    def from_cfg(cls, cfg_path, log_dir):
        return cls(cls.__create_key, cosim().file_observer(log_dir, cfg_path))

    @classmethod
    def create_time_series(cls, buffer_size: Optional[int] = None):
        c = cosim()
        observer = c.time_series_observer() if buffer_size is None else c.time_series_observer(buffer_size)
        return cls(cls.__create_key, observer)

    def start_time_series(self, slave_index: int, value_reference: int, variable_type: CosimEnums.CosimVariableType):
        try:
            self.__ptr.start_observing(
                variable_id(slave_index, to_cosim_variable_type(variable_type.value), value_reference))
            return True
        except Exception:
            return False

    def stop_time_series(self, slave_index: int, value_reference: int, variable_type: CosimEnums.CosimVariableType):
        try:
            self.__ptr.stop_observing(
                variable_id(slave_index, to_cosim_variable_type(variable_type.value), value_reference))
            return True
        except Exception:
            return False

    def __time_series_samples(self, slave_index, value_reference, from_step, sample_count, integer):
        try:
            get = self.__ptr.get_integer_samples if integer else self.__ptr.get_real_samples
            s = get(slave_index, value_reference, from_step, sample_count)
        except Exception:
            raise AssertionError(
                "Unable to retrieve samples. Check if indexes are valid, the observer is a time "
                "series and that the time series have been started and at least one step has been "
                "executed."
            )
        # libcosimpy order: (time points, step numbers, samples)
        return list(s.get_times()), list(s.steps), list(s.values)

    def time_series_real_samples(self, slave_index: int, value_reference: int, from_step: int, sample_count: int = 10):
        return self.__time_series_samples(slave_index, value_reference, from_step, sample_count, integer=False)

    def time_series_integer_samples(self, slave_index: int, value_reference: int, from_step: int, sample_count: int = 10):
        return self.__time_series_samples(slave_index, value_reference, from_step, sample_count, integer=True)

    def __last_values(self, get, slave_index, variable_references):
        try:
            return list(get(slave_index, list(variable_references)))
        except Exception:
            raise AssertionError("Unable to return values. Check if indexes are valid.")

    def last_real_values(self, slave_index: int, variable_references: list[int]):
        return self.__last_values(self.__ptr.get_real, slave_index, variable_references)

    def last_integer_values(self, slave_index: int, variable_references: list[int]):
        return self.__last_values(self.__ptr.get_integer, slave_index, variable_references)

    def last_boolean_values(self, slave_index: int, variable_references: list[int]):
        return [bool(v) for v in self.__last_values(self.__ptr.get_boolean, slave_index, variable_references)]

    def last_string_values(self, slave_index: int, variable_references: list[int]):
        # libcosimpy returns c_char_p values, i.e. bytes.
        return [v.encode() for v in self.__last_values(self.__ptr.get_string, slave_index, variable_references)]

    def ptr(self):
        return self.__ptr
