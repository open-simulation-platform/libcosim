"""End-to-end smoke tests for the SWIG Python bindings (spike).

Run with the built package on PYTHONPATH, from the repository root:

    PYTHONPATH=<build>/bindings/python python -m unittest bindings/swig/python/tests/test_spike.py
"""

import os
import pathlib
import tempfile
import unittest

import libcosim as cosim

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "..", ".."))
MSMI = os.path.join(REPO, "tests", "data", "msmi", "OspSystemStructure.xml")

cosim.set_log_level(cosim.log_level_warning)


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
        self.assertAlmostEqual(self.exe.current_time(), 0.01)

    def test_model_description(self):
        md = self.exe.get_model_description(self.sims["TrueIdentity"])
        names = {v.name: v for v in md.variables}
        self.assertIn("realIn", names)
        self.assertEqual(names["realIn"].type, cosim.variable_type_real)
        self.assertEqual(names["realIn"].causality, cosim.variable_causality_input)

    def test_step_and_simulate_until(self):
        dt = self.exe.step()
        self.assertGreater(dt, 0.0)
        self.assertTrue(self.exe.simulate_until(0.02))
        self.assertAlmostEqual(self.exe.current_time(), 0.02, places=6)

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
        self.assertEqual(len(s.values), len(s.times_seconds()))
        self.assertAlmostEqual(s.values[-1], 1.5)
        self.assertTrue(all(b > a for a, b in zip(s.times_seconds(), list(s.times_seconds())[1:])))

    def test_real_time_config(self):
        self.assertFalse(self.exe.is_real_time_simulation())
        self.exe.set_real_time_factor_target(2.0)
        self.assertAlmostEqual(self.exe.get_real_time_factor_target(), 2.0)
        with self.assertRaises(RuntimeError):
            self.exe.set_real_time_factor_target(0.0)

    def test_pathlike_and_end_time(self):
        loaded = cosim.load_execution_from_osp_config(
            pathlib.Path(REPO) / "tests" / "data" / "msmi" / "OspSystemStructure_EndTime.xml")
        self.assertTrue(loaded.has_end_time())
        self.assertAlmostEqual(loaded.end_time_seconds(), 0.001)

    def test_file_observer(self):
        with tempfile.TemporaryDirectory() as d:
            fo = cosim.file_observer(d)
            self.exe.add_observer(fo)
            self.exe.step()
            self.assertTrue(fo.is_recording())
            self.assertTrue(fo.get_log_path().startswith(d))


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
        algo = cosim.fixed_step_algorithm(1e-3)
        exe = cosim.execution(0.0, algo)
        self.assertEqual(exe.current_time(), 0.0)


if __name__ == "__main__":
    unittest.main()
