// Smoke test for the C# bindings, mirroring the Python/C/Julia/Java tests.
//
// Usage: dotnet SmokeTest.dll <OspSystemStructure.xml> <identity.fmu>
// (cosim_csharp and libcosim must be on the library search path.)

using System;
using System.Linq;
using System.Threading;
using Libcosim;

static class SmokeTest
{
    static int failures = 0;

    static void Check(bool condition, string what)
    {
        if (!condition) {
            Console.Error.WriteLine("CHECK failed: " + what);
            failures++;
        }
    }

    static uint Ref(execution exe, int sim, string name)
    {
        // `variables` returns an owned copy, safe after the temporary is gone.
        foreach (var v in exe.get_model_description(sim).variables) {
            if (v.name == name) return v.reference;
        }
        throw new ArgumentException("No variable " + name);
    }

    static void WaitUntil(Func<bool> condition)
    {
        var deadline = DateTime.Now.AddSeconds(60);
        while (!condition() && DateTime.Now < deadline) Thread.Sleep(1);
    }

    static int Main(string[] args)
    {
        cosim.set_output_level(level.warning);
        string config = args[0];
        string identityFmu = args[1];

        // Errors carry the cosim::errc code
        try {
            cosim.load_execution_from_ssp("/nonexistent/system.ssp", new execution_load_options());
            Check(false, "expected CosimException");
        } catch (CosimException e) {
            Check(e.Code == (int)errc.zip_error, "error code is zip_error");
            Console.WriteLine($"expected error: code={e.Code} {e.Message}");
        }

        // OSP config, override, last value; times are long nanoseconds
        using var loaded = cosim.load_execution_from_osp_config(config, new execution_load_options());
        var exe = loaded.execution;
        int sim = loaded.simulators()["TrueIdentity"];
        var obs = new last_value_observer();
        var man = new override_manipulator();
        exe.add_observer(obs);
        exe.add_manipulator(man);
        man.override_integer_variable(sim, Ref(exe, sim, "integerIn"), 42);
        long t0 = exe.current_time();
        Check(t0 == cosim.to_nanoseconds(0.01), "start time 0.01 s");
        Check(exe.simulate_until(t0 + cosim.to_nanoseconds(0.01)), "simulate_until");
        var values = obs.get_integer(sim, new ValueReferenceVector(new uint[] { Ref(exe, sim, "integerOut") }));
        Check(values[0] == 42, "integerOut == 42");
        Console.WriteLine($"time: {cosim.to_seconds(t0)} s -> {cosim.to_seconds(exe.current_time())} s");

        // add_fmu, get_simulators, non-blocking start/stop/status
        var exe2 = new execution(0, new fixed_step_algorithm(cosim.to_nanoseconds(0.1), 0));
        Check(cosim.add_fmu(exe2, identityFmu, "a", 0, null) == 0, "add_fmu");
        Check(exe2.get_simulators().First().name == "a", "simulator name");
        exe2.start_simulation();
        WaitUntil(() => exe2.get_status().current_time > cosim.to_nanoseconds(1.0));
        Check(exe2.get_status().state == execution_state.running, "running");
        exe2.stop_simulation();
        WaitUntil(() => !exe2.is_running());
        var status = exe2.get_status();
        Check(status.state == execution_state.stopped, "stopped");
        Check(status.error_code == errc.success, "no error");

        // ECCO algorithm from code (durations in nanoseconds)
        var p = new ecco_algorithm_params {
            safety_factor = 0.99,
            step_size = cosim.to_nanoseconds(1e-4),
            min_step_size = cosim.to_nanoseconds(1e-5),
            max_step_size = cosim.to_nanoseconds(1e-2),
            min_change_rate = 0.2,
            max_change_rate = 1.5,
            abs_tolerance = 1e-6,
            rel_tolerance = 1e-6,
            p_gain = 0.2,
            i_gain = 0.15,
        };
        Check(p.step_size == 100_000, "ECCO step size in ns");
        var exe3 = new execution(0, new ecco_algorithm(p, 0));
        Check(exe3.step() > 0, "ECCO step");

        if (failures > 0) {
            Console.Error.WriteLine($"{failures} check(s) failed");
            return 1;
        }
        Console.WriteLine("C# smoke test OK");
        return 0;
    }
}
