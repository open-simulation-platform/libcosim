package libcosim;

/**
 * Smoke test for the Java bindings, mirroring the Python/C/Julia tests.
 *
 * Usage: java -cp <libcosim.jar>:<test classes> libcosim.SmokeTest <OspSystemStructure.xml> <identity.fmu>
 *    or: java -Djava.library.path=<dir of cosim_java> -cp <classes> libcosim.SmokeTest ...
 */
public class SmokeTest {
    static int failures = 0;

    static void check(boolean condition, String what) {
        if (!condition) {
            System.err.println("CHECK failed: " + what);
            failures++;
        }
    }

    static long ref(execution exe, int sim, String name) {
        // getVariables() returns an owned copy, safe to use after the
        // temporary model description is gone.
        for (variable_description v : exe.get_model_description(sim).getVariables()) {
            if (v.getName().equals(name)) return v.getReference();
        }
        throw new IllegalArgumentException("No variable " + name);
    }

    static void waitUntil(java.util.function.BooleanSupplier condition) throws InterruptedException {
        long deadline = System.currentTimeMillis() + 60_000;
        while (!condition.getAsBoolean() && System.currentTimeMillis() < deadline) Thread.sleep(1);
    }

    public static void main(String[] args) throws Exception {
        // No explicit loading: the native libraries are loaded on first use
        // (from the JAR, or from java.library.path in a build tree).
        cosim.set_output_level(level.warning);
        String config = args[0];
        String identityFmu = args[1];

        // Errors carry the cosim::errc code
        try {
            cosim.load_execution_from_ssp("/nonexistent/system.ssp", new execution_load_options());
            check(false, "expected CosimException");
        } catch (CosimException e) {
            check(e.getCode() == errc.zip_error.swigValue(), "error code is zip_error");
            System.out.println("expected error: code=" + e.getCode() + " " + e.getMessage());
        }

        // OSP config, override, last value; times are long nanoseconds
        configured_execution loaded = cosim.load_execution_from_osp_config(config, new execution_load_options());
        execution exe = loaded.getExecution();
        int sim = loaded.simulators().get("TrueIdentity");
        last_value_observer obs = new last_value_observer();
        override_manipulator man = new override_manipulator();
        exe.add_observer(obs);
        exe.add_manipulator(man);
        man.override_integer_variable(sim, ref(exe, sim, "integerIn"), 42);
        long t0 = exe.current_time();
        check(t0 == cosim.to_nanoseconds(0.01), "start time 0.01 s");
        check(exe.simulate_until(t0 + cosim.to_nanoseconds(0.01)), "simulate_until");
        IntVector values = obs.get_integer(sim, new ValueReferenceVector(new long[] {ref(exe, sim, "integerOut")}));
        check(values.get(0) == 42, "integerOut == 42");
        System.out.println("time: " + cosim.to_seconds(t0) + " s -> " + cosim.to_seconds(exe.current_time()) + " s");

        // add_fmu, get_simulators, non-blocking start/stop/status
        execution exe2 = new execution(0, new fixed_step_algorithm(cosim.to_nanoseconds(0.1), 0));
        check(cosim.add_fmu(exe2, identityFmu, "a", 0, null) == 0, "add_fmu");
        check(exe2.get_simulators().get(0).getName().equals("a"), "simulator name");
        exe2.start_simulation();
        waitUntil(() -> exe2.get_status().getCurrent_time() > cosim.to_nanoseconds(1.0));
        check(exe2.get_status().getState() == execution_state.running, "running");
        exe2.stop_simulation();
        waitUntil(() -> !exe2.is_running());
        execution_status status = exe2.get_status();
        check(status.getState() == execution_state.stopped, "stopped");
        check(status.getError_code() == errc.success, "no error");

        // ECCO algorithm from code (durations in nanoseconds)
        ecco_algorithm_params p = new ecco_algorithm_params();
        p.setSafety_factor(0.99);
        p.setStep_size(cosim.to_nanoseconds(1e-4));
        p.setMin_step_size(cosim.to_nanoseconds(1e-5));
        p.setMax_step_size(cosim.to_nanoseconds(1e-2));
        p.setMin_change_rate(0.2);
        p.setMax_change_rate(1.5);
        p.setAbs_tolerance(1e-6);
        p.setRel_tolerance(1e-6);
        p.setP_gain(0.2);
        p.setI_gain(0.15);
        check(p.getStep_size() == 100_000, "ECCO step size in ns");
        ecco_algorithm ecco = new ecco_algorithm(p, 0);
        execution exe3 = new execution(0, ecco);
        check(exe3.step() > 0, "ECCO step");

        // State export and import as bytes (needs an FMU with state export
        // and an output "x", e.g. the Dahlquist reference FMU)
        String stateFmu = System.getenv("LIBCOSIM_STATE_FMU");
        if (stateFmu != null && !stateFmu.isEmpty()) {
            execution exe4 = new execution(0, new fixed_step_algorithm(cosim.to_nanoseconds(0.01), 0));
            int dq = cosim.add_fmu(exe4, stateFmu, "dq", 0, null);
            ValueReferenceVector x = new ValueReferenceVector(new long[] {ref(exe4, dq, "x")});
            last_value_observer obs4 = new last_value_observer();
            exe4.add_observer(obs4);
            exe4.simulate_until(cosim.to_nanoseconds(0.5));
            ByteVector state = exe4.export_current_state_bytes();
            check(state.size() > 0, "state exported");
            exe4.simulate_until(cosim.to_nanoseconds(1.0));
            double expected = obs4.get_real(dq, x).get(0);
            exe4.import_state_bytes(state);
            check(exe4.current_time() == cosim.to_nanoseconds(0.5), "time restored");
            exe4.simulate_until(cosim.to_nanoseconds(1.0));
            check(obs4.get_real(dq, x).get(0) == expected, "same result after import");
            try {
                exe4.import_state_bytes(new ByteVector(new short[] {0xff, 0x00, 0x13}));
                check(false, "expected CosimException for invalid state");
            } catch (CosimException e) {
                check(e.getCode() == errc.bad_file.swigValue(), "invalid state is bad_file");
            }
        } else {
            System.out.println("state export/import: skipped (LIBCOSIM_STATE_FMU not set)");
        }

        if (failures > 0) {
            System.err.println(failures + " check(s) failed");
            System.exit(1);
        }
        System.out.println("Java smoke test OK");
    }
}
