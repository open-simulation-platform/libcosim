"""End-to-end tests for the libcosim Python bindings.

Run with the built package on PYTHONPATH (or the wheel installed):

    PYTHONPATH=<build>/bindings/python python -m unittest bindings/swig/python/tests/test_bindings.py

The test data is taken from libcosim's tests/data, or LIBCOSIM_TEST_DATA.
"""

import os
import pathlib
import tempfile
import unittest

import libcosim as cosim

# libcosim's test data (tests/data in the libcosim repository)
DATA = os.environ.get("LIBCOSIM_TEST_DATA") or os.path.abspath(
    os.path.join(os.path.dirname(__file__), "..", "..", "..", "..", "tests", "data"))
MSMI = os.path.join(DATA, "msmi", "OspSystemStructure.xml")
IDENTITY = os.path.join(DATA, "fmi1", "identity.fmu")
QUARTER_TRUCK = os.path.join(DATA, "fmi2", "quarter_truck")

cosim.set_output_level(cosim.level_warning)


def refs_by_name(exe, sim):
    md = exe.get_model_description(sim)
    return {v.name: v.reference for v in md.variables}


class ExecutionTest(unittest.TestCase):
    def setUp(self):
        self.loaded = cosim.load_execution_from_osp_config(MSMI)
        self.exe = self.loaded.execution
        self.sims = dict(self.loaded.simulators())

    def test_load(self):
        self.assertEqual(set(self.sims), {"CraneController", "KnuckleBoomCrane", "TrueIdentity", "OneIdentity"})
        self.assertFalse(self.loaded.has_end_time())
        # Times are int nanoseconds, as in libcosimc/libcosimpy
        self.assertEqual(self.exe.current_time(), 10_000_000)
        self.assertEqual(self.exe.current_time(), cosim.to_nanoseconds(0.01))
        self.assertAlmostEqual(cosim.to_seconds(self.exe.current_time()), 0.01)

    def test_model_description(self):
        md = self.exe.get_model_description(self.sims["TrueIdentity"])
        names = {v.name: v for v in md.variables}
        self.assertIn("realIn", names)
        self.assertEqual(names["realIn"].type, cosim.variable_type_real)
        self.assertEqual(names["realIn"].causality, cosim.variable_causality_input)

    def test_keyword_safe_enum_names(self):
        # Same names in all bindings for enumerators that are keywords in
        # Java/C# (C++: variable_type::boolean/string, variable_variability::fixed).
        for name in ("variable_type_boolean_value", "variable_type_string_value",
                     "variable_variability_fixed_value"):
            self.assertTrue(hasattr(cosim, name), name)
        for name in ("variable_type_boolean", "variable_type_string", "variable_variability_fixed"):
            self.assertFalse(hasattr(cosim, name), name)

    def test_member_of_temporary_is_a_copy(self):
        # Struct members of class type are returned by value. Without that,
        # .variables would point into the temporary model description and
        # iterating it would read freed memory.
        sim = self.sims["TrueIdentity"]
        names = [v.name for v in self.exe.get_model_description(sim).variables]
        self.assertIn("realIn", names)
        first = self.exe.get_model_description(sim).variables[0]
        self.assertTrue(first.name)
        loaded = cosim.load_execution_from_osp_config(MSMI)
        exe = loaded.execution
        del loaded
        self.assertGreater(exe.step(), 0)

    def test_step_and_simulate_until(self):
        dt = self.exe.step()
        self.assertGreater(dt, 0.0)
        self.assertIsInstance(dt, int)
        self.assertTrue(self.exe.simulate_until(cosim.to_nanoseconds(0.02)))
        self.assertEqual(self.exe.current_time(), 20_000_000)

    def test_float_time_rejected(self):
        with self.assertRaises(TypeError) as ctx:
            self.exe.simulate_until(0.02)
        self.assertIn("to_nanoseconds", str(ctx.exception))

    def test_override_and_last_value(self):
        sim = self.sims["TrueIdentity"]
        refs = refs_by_name(self.exe, sim)
        obs = cosim.last_value_observer()
        man = cosim.override_manipulator()
        self.exe.add_observer(obs)
        self.exe.add_manipulator(man)

        man.override_integer_variable(sim, refs["integerIn"], 42)
        man.override_string_variable(sim, refs["stringIn"], "hello")
        man.override_boolean_variable(sim, refs["booleanIn"], False)
        self.exe.step()
        self.exe.step()

        self.assertEqual(list(obs.get_integer(sim, [refs["integerOut"]])), [42])
        self.assertEqual(list(obs.get_string(sim, [refs["stringOut"]])), ["hello"])
        self.assertEqual(list(obs.get_boolean(sim, [refs["booleanOut"]])), [False])

    def test_time_series(self):
        sim = self.sims["TrueIdentity"]
        refs = refs_by_name(self.exe, sim)
        ts = cosim.time_series_observer()
        man = cosim.override_manipulator()
        self.exe.add_observer(ts)
        self.exe.add_manipulator(man)
        vid = cosim.variable_id()
        vid.simulator = sim
        vid.type = cosim.variable_type_real
        vid.reference = refs["realOut"]
        ts.start_observing(vid)

        man.override_real_variable(sim, refs["realIn"], 1.5)
        for _ in range(10):
            self.exe.step()

        s = ts.get_real_samples(sim, refs["realOut"], 1, 100)
        self.assertGreater(len(s.values), 0)
        self.assertEqual(len(s.values), len(s.steps))
        self.assertEqual(len(s.values), len(s.get_times()))
        self.assertEqual(len(s.values), len(s.get_times_seconds()))
        self.assertTrue(all(isinstance(t, int) for t in s.get_times()))
        self.assertAlmostEqual(s.values[-1], 1.5)
        self.assertTrue(all(b > a for a, b in zip(s.get_times(), list(s.get_times())[1:])))

    def test_real_time_config(self):
        self.assertFalse(self.exe.is_real_time_simulation())
        self.exe.set_real_time_factor_target(2.0)
        self.assertAlmostEqual(self.exe.get_real_time_factor_target(), 2.0)
        with self.assertRaises(RuntimeError):
            self.exe.set_real_time_factor_target(0.0)

    def test_pathlike_and_end_time(self):
        loaded = cosim.load_execution_from_osp_config(
            pathlib.Path(DATA) / "msmi" / "OspSystemStructure_EndTime.xml")
        self.assertTrue(loaded.has_end_time())
        self.assertEqual(loaded.get_end_time(), 1_000_000)
        self.assertAlmostEqual(loaded.get_end_time_seconds(), 0.001)

    def test_load_options(self):
        options = cosim.execution_load_options()
        options.set_start_time(cosim.to_nanoseconds(1.0))
        options.set_worker_thread_count(2)
        options.algorithm = cosim.fixed_step_algorithm(cosim.to_nanoseconds(1e-4))
        loaded = cosim.load_execution_from_osp_config(MSMI, options)
        self.assertEqual(loaded.execution.current_time(), 1_000_000_000)
        self.assertTrue(loaded.execution.simulate_until(cosim.to_nanoseconds(1.001)))

    def test_file_observer(self):
        with tempfile.TemporaryDirectory() as d:
            # A separate execution, released before the directory is removed:
            # on Windows, files still open in the observer cannot be deleted.
            exe = cosim.load_execution_from_osp_config(MSMI).execution
            fo = cosim.file_observer(d)
            exe.add_observer(fo)
            exe.step()
            self.assertTrue(fo.is_recording())
            self.assertTrue(fo.get_log_path().startswith(d))
            del exe, fo
            import gc
            gc.collect()


def wait_until(predicate, timeout=10.0):
    import time
    deadline = time.monotonic() + timeout
    while not predicate():
        if time.monotonic() > deadline:
            return False
        time.sleep(0.001)
    return True


class ApiAdditionsTest(unittest.TestCase):
    def new_execution(self, step=0.1):
        return cosim.execution(0, cosim.fixed_step_algorithm(cosim.to_nanoseconds(step)))

    def test_add_fmu_and_get_simulators(self):
        exe = self.new_execution()
        a = cosim.add_fmu(exe, IDENTITY, "a")
        b = cosim.add_fmu(exe, pathlib.Path(IDENTITY), "b", cosim.to_nanoseconds(0.2))
        self.assertEqual((a, b), (0, 1))
        self.assertEqual([(s.name, s.index) for s in exe.get_simulators()], [("a", 0), ("b", 1)])
        self.assertTrue(exe.simulate_until(cosim.to_nanoseconds(0.5)))
        with self.assertRaises(RuntimeError):
            cosim.add_fmu(exe, "/no/such.fmu", "c")

    def test_start_stop_status(self):
        exe = self.new_execution()
        cosim.add_fmu(exe, IDENTITY, "a")
        status = exe.get_status()
        self.assertEqual(status.state, cosim.execution_state_stopped)
        self.assertEqual(status.error_code, cosim.errc_success)
        self.assertEqual(status.current_time, 0)

        exe.start_simulation()
        self.assertEqual(exe.get_status().state, cosim.execution_state_running)
        self.assertTrue(wait_until(lambda: exe.get_status().current_time > cosim.to_nanoseconds(1.0)))
        with self.assertRaises(cosim.CosimError):
            exe.start_simulation()
        exe.stop_simulation()
        self.assertTrue(wait_until(lambda: exe.get_status().state == cosim.execution_state_stopped))
        self.assertFalse(exe.is_running())

    def test_ecco_from_code(self):
        params = cosim.ecco_algorithm_params()
        params.safety_factor = 0.99
        params.step_size = cosim.to_nanoseconds(1e-4)
        params.min_step_size = cosim.to_nanoseconds(1e-5)
        params.max_step_size = cosim.to_nanoseconds(1e-2)
        params.min_change_rate = 0.2
        params.max_change_rate = 1.5
        params.abs_tolerance = 1e-6
        params.rel_tolerance = 1e-6
        params.p_gain = 0.2
        params.i_gain = 0.15
        self.assertEqual(params.step_size, 100_000)

        algo = cosim.ecco_algorithm(params)
        exe = cosim.execution(0, algo)
        chassis = cosim.add_fmu(exe, os.path.join(QUARTER_TRUCK, "Chassis.fmu"), "chassis")
        wheel = cosim.add_fmu(exe, os.path.join(QUARTER_TRUCK, "Wheel.fmu"), "wheel")
        refs_c, refs_w = refs_by_name(exe, chassis), refs_by_name(exe, wheel)
        exe.set_real_initial_value(chassis, refs_c["mass"], 400.0)
        exe.set_real_initial_value(wheel, refs_w["mass"], 40.0)

        def var(sim, ref):
            v = cosim.variable_id()
            v.simulator, v.type, v.reference = sim, cosim.variable_type_real, ref
            return v

        # FMU-level causalities (the "causality" attributes of power-bond
        # variables in OSP files describe the bond, not the FMU variables).
        input_a, output_a = var(chassis, refs_c["force"]), var(chassis, refs_c["velocity"])
        input_b, output_b = var(wheel, refs_w["in_vel"]), var(wheel, refs_w["out_spring_damper_f"])
        exe.connect_variables(output_a, input_b)
        exe.connect_variables(output_b, input_a)
        algo.add_power_bond(input_a, output_a, input_b, output_b)

        self.assertTrue(exe.simulate_until(cosim.to_nanoseconds(0.01)))
        self.assertGreater(exe.current_time(), 0)
        self.assertGreater(len(algo.get_powerbond_energies(chassis)), 0)


class PythonicTest(unittest.TestCase):
    """The thin Pythonic layer (python/pythonic.py)."""

    def setUp(self):
        self.exe = cosim.load_execution_from_osp_config(MSMI).execution

    def test_names(self):
        sim = self.exe.simulator_index("TrueIdentity")
        self.assertEqual(self.exe.variables(sim).keys(), self.exe.variables("TrueIdentity").keys())
        self.assertIn("realIn", self.exe.variables("TrueIdentity"))
        var = self.exe.variable("TrueIdentity", "realOut")
        self.assertEqual((var.simulator, var.type), (sim, cosim.variable_type_real))
        with self.assertRaises(KeyError):
            self.exe.simulator_index("NoSuchSimulator")
        with self.assertRaises(KeyError):
            self.exe.variable("TrueIdentity", "noSuchVariable")

    def test_values_of_any_type(self):
        obs = cosim.last_value_observer()
        man = cosim.override_manipulator()
        self.exe.add_observer(obs)
        self.exe.add_manipulator(man)
        values = {"real": 1.5, "integer": 42, "boolean": False, "string": "hello"}
        for kind, value in values.items():
            man.override(self.exe.variable("TrueIdentity", f"{kind}In"), value)
        self.exe.step()
        self.exe.step()
        for kind, value in values.items():
            self.assertEqual(obs.value(self.exe.variable("TrueIdentity", f"{kind}Out")), value)

        real_in = self.exe.variable("TrueIdentity", "realIn")
        man.reset(real_in)
        self.exe.step()
        self.exe.step()
        self.assertNotEqual(obs.value(real_in), 1.5)

    def test_initial_value_and_samples(self):
        exe = cosim.execution(0, cosim.fixed_step_algorithm(cosim.to_nanoseconds(0.1)))
        cosim.add_fmu(exe, IDENTITY, "id")
        exe.set_initial_value(exe.variable("id", "integerIn"), 7)
        ts = cosim.time_series_observer()
        exe.add_observer(ts)
        out = exe.variable("id", "integerOut")
        ts.start_observing(out)
        exe.simulate_until(cosim.to_nanoseconds(0.5))
        samples = ts.samples(out, 1, 10)
        self.assertGreater(len(samples.values), 0)
        self.assertEqual(set(samples.values), {7})
        with self.assertRaises(TypeError):
            ts.samples(exe.variable("id", "stringOut"), 1, 10)

    def test_running(self):
        exe = cosim.execution(0, cosim.fixed_step_algorithm(cosim.to_nanoseconds(0.1)))
        cosim.add_fmu(exe, IDENTITY, "id")
        with exe.running():
            self.assertTrue(wait_until(lambda: exe.current_time() > cosim.to_nanoseconds(1.0)))
        self.assertEqual(exe.get_status().state, cosim.execution_state_stopped)
        self.assertFalse(exe.is_running())

    def test_repr(self):
        var = self.exe.variable("TrueIdentity", "realOut")
        self.assertIn("type=real", repr(var))
        desc = self.exe.variables("TrueIdentity")["realIn"]
        self.assertIn("name='realIn'", repr(desc))
        self.assertIn("causality=input", repr(desc))
        self.assertIn("state=stopped", repr(self.exe.get_status()))
        self.assertIn("simulator_info(name=", repr(self.exe.get_simulators()[0]))


class LoadingTest(unittest.TestCase):
    def test_version(self):
        v = cosim.library_version()
        self.assertGreaterEqual((v.major, v.minor, v.patch), (0, 0, 0))

    def test_ssp(self):
        loaded = cosim.load_execution_from_ssp(os.path.join(DATA, "ssp", "demo"))
        self.assertGreater(len(dict(loaded.simulators())), 0)
        self.assertGreater(loaded.execution.step(), 0)


STATE_FMU = os.environ.get("LIBCOSIM_STATE_FMU")


@unittest.skipUnless(STATE_FMU, "needs an FMU with state export (LIBCOSIM_STATE_FMU)")
class StateTest(unittest.TestCase):
    """Exporting and importing the execution state as bytes (checkpoints).

    Uses the Dahlquist reference FMU (variable x), or any FMU with an output
    x that supports state export, given by LIBCOSIM_STATE_FMU.
    """

    def new_execution(self):
        exe = cosim.execution(0, cosim.fixed_step_algorithm(cosim.to_nanoseconds(0.01)))
        cosim.add_fmu(exe, STATE_FMU, "dq")
        return exe

    def test_round_trip(self):
        exe = self.new_execution()
        caps = exe.get_model_description(0).capabilities
        self.assertTrue(caps.can_save_state and caps.can_export_state)
        obs = cosim.last_value_observer()
        exe.add_observer(obs)
        x = exe.variable("dq", "x")

        exe.simulate_until(cosim.to_nanoseconds(0.5))
        state = exe.export_current_state_bytes()
        self.assertIsInstance(state, bytes)
        self.assertGreater(len(state), 0)
        exe.simulate_until(cosim.to_nanoseconds(1.0))
        expected = obs.value(x)

        exe.import_state_bytes(state)
        self.assertEqual(exe.current_time(), cosim.to_nanoseconds(0.5))
        exe.simulate_until(cosim.to_nanoseconds(1.0))
        self.assertEqual(obs.value(x), expected)

        exe.import_state_bytes(bytearray(state))  # bytearray works too
        self.assertEqual(exe.current_time(), cosim.to_nanoseconds(0.5))

    def test_errors(self):
        exe = self.new_execution()
        with self.assertRaises(cosim.CosimError) as ctx:
            exe.export_current_state_bytes()  # not initialized
        self.assertEqual(ctx.exception.args[1], cosim.errc_invalid_operation)
        exe.step()
        with self.assertRaises(cosim.CosimError) as ctx:
            exe.import_state_bytes(b"\xff\x00\x13")
        self.assertEqual(ctx.exception.args[1], cosim.errc_bad_file)
        with self.assertRaises(TypeError):
            exe.import_state_bytes("not bytes")


class ErrorTest(unittest.TestCase):
    def test_missing_config_raises(self):
        with self.assertRaises(RuntimeError):
            cosim.load_execution_from_osp_config("/nonexistent/OspSystemStructure.xml")

    def test_cosim_error_has_code(self):
        with self.assertRaises(cosim.CosimError) as ctx:
            cosim.load_execution_from_ssp("/nonexistent/system.ssp")
        message, code = ctx.exception.args
        self.assertIsInstance(message, str)
        self.assertIsInstance(code, int)

    def test_algorithm_construction(self):
        algo = cosim.fixed_step_algorithm(cosim.to_nanoseconds(1e-3))
        exe = cosim.execution(0, algo)
        self.assertEqual(exe.current_time(), 0)


if __name__ == "__main__":
    unittest.main()
