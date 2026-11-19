import time
from typing import Optional

from ._internal import (
    cosim,
    error_code_of,
    from_cosim_causality,
    from_cosim_variability,
    from_cosim_variable_type,
    set_last_error,
    to_libcosimc_error_code,
    to_nanoseconds_int,
    variable_id,
)
from . import CosimAlgorithm, CosimConstants, CosimEnums, CosimManipulator, CosimObserver, CosimSlave

# libcosimc's ILLEGAL_STATE error code
_ILLEGAL_STATE = CosimEnums.CosimErrorCode.ILLEGAL_STATE.value


class CosimExecutionStatus:
    """Execution status, with the fields of libcosimpy's CosimExecutionStatus."""

    def __init__(self):
        self.current_time = 0
        self.state = CosimEnums.CosimExecutionState.STOPPED.value
        self.error_code = CosimEnums.CosimErrorCode.SUCCESS.value
        self.real_time_factor = 1.0
        self.rolling_average_real_time_factor = 1.0
        self.real_time_factor_target = 1.0
        self.is_real_time_simulation = 0
        self.steps_to_monitor = 0


class CosimExecution:
    __create_key: object = object()

    def __init__(self, create_key: object = None, execution_ptr=None):
        assert create_key is CosimExecution.__create_key, (
            "Execution can only be initialized using the CosimExecution.from_* methods"
        )
        self.__exe = execution_ptr
        # libcosimc keeps its own state/error code for synchronous failures.
        self.__failed = False
        self.__error_code = CosimEnums.CosimErrorCode.SUCCESS.value
        self.execution_status = CosimExecutionStatus()

    # --- creation ----------------------------------------------------------------

    @classmethod
    def from_algorithm(cls, algorithm: CosimAlgorithm.CosimAlgorithm):
        try:
            return cls(cls.__create_key, cosim().execution(0, algorithm.ptr))
        except Exception:
            raise RuntimeError("Unable to create execution from algorithm")

    @classmethod
    def from_step_size(cls, step_size):
        step = to_nanoseconds_int(step_size, "Step size")
        assert step_size > 0, "Step size must be a positive and non-zero integer"
        c = cosim()
        return cls(cls.__create_key, c.execution(0, c.fixed_step_algorithm(step)))

    @classmethod
    def from_osp_config_file(cls, osp_path: str):
        if not isinstance(osp_path, str):
            raise AttributeError("Unable to encode OSP path")
        try:
            exe = cosim().load_execution_from_osp_config(osp_path).execution
        except Exception as e:
            set_last_error(e)
            raise AssertionError(f"Unable to create execution from path: {e}")
        return cls(cls.__create_key, exe)

    @classmethod
    def from_ssp_file(cls, ssp_path: str, step_size=None):
        if not isinstance(ssp_path, str):
            raise AttributeError(f"'{type(ssp_path).__name__}' object has no attribute 'encode'")
        c = cosim()
        options = c.execution_load_options()
        if step_size is not None:
            step = to_nanoseconds_int(step_size, "Step size")
            assert step_size > 0, "Step size must be a positive and non-zero integer"
            options.algorithm = c.fixed_step_algorithm(step)
        try:
            exe = c.load_execution_from_ssp(ssp_path, options).execution
        except Exception as e:
            set_last_error(e)
            raise AssertionError("Unable to create execution from path. Please check if path is correct.")
        return cls(cls.__create_key, exe)

    # --- helpers -------------------------------------------------------------------

    def __fail(self, exception, code=None) -> None:
        set_last_error(exception)
        self.__failed = True
        self.__error_code = code if code is not None else error_code_of(exception)

    def __call(self, function, *args):
        """Calls function; on error records it libcosimc-style and returns None."""
        try:
            self.__failed = False
            self.__error_code = CosimEnums.CosimErrorCode.SUCCESS.value
            result = function(*args)
            return True if result is None else result
        except Exception as e:
            self.__fail(e)
            return None

    # --- running -------------------------------------------------------------------

    def num_slaves(self):
        return len(self.__exe.get_simulators())

    def start(self):
        if self.__exe.is_running():
            return True  # as libcosimc: starting a running execution is a no-op
        return self.__call(self.__exe.start_simulation) is not None

    def stop(self):
        self.__exe.stop_simulation()
        # libcosimc waits for the background run to finish.
        while self.__exe.is_running():
            time.sleep(0.001)
        status = self.__exe.get_status()
        if status.state == cosim().execution_state_error:
            self.__failed = True
            self.__error_code = to_libcosimc_error_code(status.error_code)
            return False
        return True

    def simulate_until(self, target_time):
        target = to_nanoseconds_int(target_time, "Target time")
        assert target > 0, "Target time must be a positive and non-zero integer"
        if self.__exe.is_running():
            self.__fail(RuntimeError(
                "Function 'cosim_execution_simulate_until' may not be called while simulation is running!"),
                _ILLEGAL_STATE)
            return False
        result = self.__call(self.__exe.simulate_until, target)
        return result is not None and bool(result)

    def step(self, step_count: int = 1):
        if self.__exe.is_running():
            return True  # as libcosimc
        for _ in range(step_count):
            if self.__call(self.__exe.step) is None:
                return False
        return True

    def real_time_simulation_enabled(self, enabled: bool = True):
        return self.__call(self.__exe.set_real_time_simulation, bool(enabled)) is not None

    def real_time_factor_target(self, real_time_factor: float):
        # libcosimc accepts any value; libcosim now rejects non-positive
        # targets. Keep libcosimpy's contract (success) but ignore such values.
        if float(real_time_factor) <= 0.0:
            return True
        return self.__call(self.__exe.set_real_time_factor_target, float(real_time_factor)) is not None

    def steps_to_monitor(self, step_count: int):
        return self.__call(self.__exe.set_steps_to_monitor, int(step_count)) is not None

    def add_manipulator(self, manipulator: CosimManipulator.CosimManipulator):
        return self.__call(self.__exe.add_manipulator, manipulator.ptr()) is not None

    def add_observer(self, observer: CosimObserver.CosimObserver):
        return self.__call(self.__exe.add_observer, observer.ptr()) is not None

    def load_scenario(self, manipulator: CosimManipulator.CosimManipulator, scenario_file: str):
        return self.__call(
            manipulator.ptr().load_scenario, scenario_file, self.__exe.current_time()) is not None

    def status(self):
        s = self.__exe.get_status()
        status = self.execution_status
        status.current_time = s.current_time
        if s.state == cosim().execution_state_error:
            status.state = CosimEnums.CosimExecutionState.ERROR.value
            status.error_code = to_libcosimc_error_code(s.error_code)
        elif self.__failed:
            status.state = CosimEnums.CosimExecutionState.ERROR.value
            status.error_code = self.__error_code
        else:
            status.state = (CosimEnums.CosimExecutionState.RUNNING.value
                            if s.state == cosim().execution_state_running
                            else CosimEnums.CosimExecutionState.STOPPED.value)
            status.error_code = CosimEnums.CosimErrorCode.SUCCESS.value
        status.real_time_factor = s.total_average_real_time_factor
        status.rolling_average_real_time_factor = s.rolling_average_real_time_factor
        status.real_time_factor_target = s.real_time_factor_target
        status.is_real_time_simulation = 1 if s.real_time_simulation else 0
        status.steps_to_monitor = self.__exe.get_steps_to_monitor()
        return status

    # --- simulators ------------------------------------------------------------------

    def slave_infos(self):
        return [CosimSlave.CosimSlaveInfo(i.name.encode(), i.index) for i in self.__exe.get_simulators()]

    def add_local_slave(self, local_slave: CosimSlave.CosimLocalSlave):
        try:
            return cosim().add_fmu(self.__exe, local_slave.fmu_path, local_slave.instance_name)
        except Exception as e:
            self.__fail(e)
            return CosimConstants.failure

    def slave_index_from_instance_name(self, instance_name: str) -> Optional[int]:
        for info in self.__exe.get_simulators():
            if info.name == instance_name:
                return info.index
        return None

    def num_slave_variables(self, slave_index: int):
        return len(self.__exe.get_model_description(slave_index).variables)

    def slave_variables(self, slave_index: int):
        return [
            CosimSlave.CosimSlaveVariableDescription(
                v.name.encode(),
                v.reference,
                from_cosim_variable_type(v.type),
                from_cosim_causality(v.causality),
                from_cosim_variability(v.variability),
            )
            for v in self.__exe.get_model_description(slave_index).variables
        ]

    # --- initial values ----------------------------------------------------------------

    def real_initial_value(self, slave_index: int, variable_reference: int, value: float):
        return self.__call(self.__exe.set_real_initial_value, slave_index, variable_reference, float(value)) is not None

    def integer_initial_value(self, slave_index: int, variable_reference: int, value: int):
        return self.__call(self.__exe.set_integer_initial_value, slave_index, variable_reference, int(value)) is not None

    def boolean_initial_value(self, slave_index: int, variable_reference: int, value: bool):
        return self.__call(self.__exe.set_boolean_initial_value, slave_index, variable_reference, bool(value)) is not None

    def string_initial_value(self, slave_index: int, variable_reference: int, value: str):
        if isinstance(value, bytes):
            value = value.decode()
        return self.__call(self.__exe.set_string_initial_value, slave_index, variable_reference, value) is not None

    # --- connections ---------------------------------------------------------------------

    def __connect(self, cosim_type, output_slave_index, output_variable_reference, input_slave_index,
                  input_variable_reference):
        result = self.__call(
            self.__exe.connect_variables,
            variable_id(output_slave_index, cosim_type, output_variable_reference),
            variable_id(input_slave_index, cosim_type, input_variable_reference),
        )
        return CosimConstants.success if result is not None else CosimConstants.failure

    def connect_real_variables(self, output_slave_index, output_variable_reference, input_slave_index,
                               input_variable_reference):
        return self.__connect(cosim().variable_type_real, output_slave_index, output_variable_reference,
                              input_slave_index, input_variable_reference)

    def connect_integer_variables(self, output_slave_index, output_variable_reference, input_slave_index,
                                  input_variable_reference):
        return self.__connect(cosim().variable_type_integer, output_slave_index, output_variable_reference,
                              input_slave_index, input_variable_reference)

    def connect_string_variables(self, output_slave_index, output_variable_reference, input_slave_index,
                                 input_variable_reference):
        return self.__connect(cosim().variable_type_string, output_slave_index, output_variable_reference,
                              input_slave_index, input_variable_reference)

    def connect_boolean_variables(self, output_slave_index, output_variable_reference, input_slave_index,
                                  input_variable_reference):
        return self.__connect(cosim().variable_type_boolean, output_slave_index, output_variable_reference,
                              input_slave_index, input_variable_reference)
