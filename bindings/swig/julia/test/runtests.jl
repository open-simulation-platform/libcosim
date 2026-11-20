# Tests the Libcosim Julia package (installed or build tree).
# Usage: julia --project=<package dir> runtests.jl <OspSystemStructure.xml> <identity.fmu>
using Libcosim
using Test

const L = Libcosim
config, identity_fmu = ARGS[1], ARGS[2]

L.cosim_set_output_level(L.cosim_level_warning)

@testset "Libcosim" begin
    v = L.cosim_library_version()
    @test L.cosim_version_major_get(v) >= 0

    # libcosim errors become CosimError with the cosim::errc code
    err = try
        L.cosim_load_execution_from_ssp("/nonexistent/system.ssp", C_NULL); nothing
    catch e
        e
    end
    @test err isa L.CosimError && err.code == L.cosim_errc_zip_error

    # OSP config, override and last value; Int64 nanosecond times
    loaded = L.cosim_load_execution_from_osp_config(config, C_NULL)
    exe = L.cosim_configured_execution_get_execution(loaded)
    sims = L.cosim_configured_execution_simulators(loaded)
    sim = L.cosim_StringIntMap_get(sims, "TrueIdentity")
    obs = L.cosim_last_value_observer_new()
    man = L.cosim_override_manipulator_new()
    L.cosim_execution_add_observer(exe, obs)
    L.cosim_execution_add_manipulator(exe, man)
    L.cosim_override_manipulator_override_integer_variable(man, sim, 0, 7)  # identity: integerIn = 0
    t0 = L.cosim_execution_current_time(exe)
    @test t0 == L.cosim_to_nanoseconds(0.01)
    @test L.cosim_execution_simulate_until(exe, t0 + L.cosim_to_nanoseconds(0.01))
    refs = L.cosim_ValueReferenceVector_new()
    L.cosim_ValueReferenceVector_push_back(refs, 0)                          # integerOut = 0
    vals = L.cosim_last_value_observer_get_integer(obs, sim, refs)
    @test L.cosim_IntVector_get(vals, 0) == 7

    # Strings come back as Julia Strings
    md = L.cosim_execution_get_model_description(exe, sim)
    @test L.cosim_model_description_name_get(md) isa String

    # add_fmu and simulator infos
    algo = L.cosim_fixed_step_algorithm_create(L.cosim_to_nanoseconds(0.1), 0)
    exe2 = L.cosim_execution_new(0, algo)
    @test L.cosim_add_fmu(exe2, identity_fmu, "a", 0, C_NULL) == 0
    infos = L.cosim_execution_get_simulators(exe2)
    @test L.cosim_simulator_info_name_get(L.cosim_SimulatorInfoVector_get(infos, 0)) == "a"
end

# Conformance items beyond the basics (see docs/conformance.md)
const data = dirname(dirname(config))

function find_ref(exe, sim, name)
    vars = L.cosim_model_description_get_variables(L.cosim_execution_get_model_description(exe, sim))
    for i in 0:Int(L.cosim_VariableDescriptionVector_size(vars))-1
        v = L.cosim_VariableDescriptionVector_get(vars, i)
        L.cosim_variable_description_name_get(v) == name && return L.cosim_variable_description_reference_get(v)
    end
    error("no variable $name")
end

function read_one(getter, obs, sim, ref)
    refs = L.cosim_ValueReferenceVector_new()
    L.cosim_ValueReferenceVector_push_back(refs, ref)
    return getter(obs, sim, refs)
end

@testset "Libcosim conformance" begin
    # CF-11 SSP
    ssp = L.cosim_load_execution_from_ssp(joinpath(data, "ssp", "demo"), C_NULL)
    @test L.cosim_StringIntMap_size(L.cosim_configured_execution_simulators(ssp)) > 0

    # CF-12 load options, CF-13 end time
    options = L.cosim_execution_load_options_new()
    L.cosim_execution_load_options_set_start_time(options, L.cosim_to_nanoseconds(1.0))
    started = L.cosim_load_execution_from_osp_config(config, options)
    @test L.cosim_execution_current_time(L.cosim_configured_execution_get_execution(started)) == L.cosim_to_nanoseconds(1.0)
    ended = L.cosim_load_execution_from_osp_config(joinpath(data, "msmi", "OspSystemStructure_EndTime.xml"), C_NULL)
    @test L.cosim_configured_execution_has_end_time(ended)
    @test L.cosim_configured_execution_get_end_time(ended) == L.cosim_to_nanoseconds(0.001)

    # An execution with one identity FMU (inputs copied to outputs)
    exe = L.cosim_execution_new(0, L.cosim_fixed_step_algorithm_create(L.cosim_to_nanoseconds(0.1), 0))
    sim = L.cosim_add_fmu(exe, identity_fmu, "id", 0, C_NULL)
    ref(name) = find_ref(exe, sim, name)
    obs = L.cosim_last_value_observer_new()
    man = L.cosim_override_manipulator_new()
    series = L.cosim_time_series_observer_create(100)
    L.cosim_execution_add_observer(exe, obs)
    L.cosim_execution_add_observer(exe, series)
    L.cosim_execution_add_manipulator(exe, man)
    observed = L.cosim_variable_id_new()
    L.cosim_variable_id_simulator_set(observed, sim)
    L.cosim_variable_id_type_set(observed, L.cosim_variable_type_integer)
    L.cosim_variable_id_reference_set(observed, ref("integerOut"))
    L.cosim_time_series_observer_start_observing(series, observed)

    # CF-23 initial values and CF-60 last values, all four types
    L.cosim_execution_set_real_initial_value(exe, sim, ref("realIn"), 2.5)
    L.cosim_execution_set_integer_initial_value(exe, sim, ref("integerIn"), 7)
    L.cosim_execution_set_boolean_initial_value(exe, sim, ref("booleanIn"), true)
    L.cosim_execution_set_string_initial_value(exe, sim, ref("stringIn"), "initial")
    L.cosim_execution_step(exe); L.cosim_execution_step(exe)
    @test L.cosim_DoubleVector_get(read_one(L.cosim_last_value_observer_get_real, obs, sim, ref("realOut")), 0) == 2.5
    @test L.cosim_IntVector_get(read_one(L.cosim_last_value_observer_get_integer, obs, sim, ref("integerOut")), 0) == 7
    @test L.cosim_BoolVector_get(read_one(L.cosim_last_value_observer_get_boolean, obs, sim, ref("booleanOut")), 0) == true
    @test L.cosim_StringVector_get(read_one(L.cosim_last_value_observer_get_string, obs, sim, ref("stringOut")), 0) == "initial"

    # CF-61 overrides and reset
    L.cosim_override_manipulator_override_real_variable(man, sim, ref("realIn"), 1.5)
    L.cosim_override_manipulator_override_string_variable(man, sim, ref("stringIn"), "hello")
    L.cosim_execution_step(exe); L.cosim_execution_step(exe)
    @test L.cosim_DoubleVector_get(read_one(L.cosim_last_value_observer_get_real, obs, sim, ref("realOut")), 0) == 1.5
    @test L.cosim_StringVector_get(read_one(L.cosim_last_value_observer_get_string, obs, sim, ref("stringOut")), 0) == "hello"
    L.cosim_override_manipulator_reset_variable(man, sim, L.cosim_variable_type_real, ref("realIn"))
    L.cosim_execution_step(exe); L.cosim_execution_step(exe)
    @test L.cosim_DoubleVector_get(read_one(L.cosim_last_value_observer_get_real, obs, sim, ref("realOut")), 0) != 1.5

    # CF-62 time series
    samples = L.cosim_time_series_observer_get_integer_samples(series, sim, ref("integerOut"), 1, 10)
    values = L.cosim_IntegerSamples_get_values(samples)
    @test L.cosim_IntVector_size(values) > 0
    @test L.cosim_IntVector_get(values, 0) == 7

    # CF-52 real-time configuration; an invalid factor is an error
    L.cosim_execution_set_real_time_factor_target(exe, 2.0)
    @test L.cosim_execution_get_real_time_factor_target(exe) == 2.0
    @test_throws L.CosimError L.cosim_execution_set_real_time_factor_target(exe, 0.0)

    # CF-51 asynchronous run
    L.cosim_execution_set_real_time_simulation(exe, false)
    L.cosim_execution_start_simulation(exe)
    @test L.cosim_execution_status_state_get(L.cosim_execution_get_status(exe)) == L.cosim_execution_state_running
    @test_throws L.CosimError L.cosim_execution_start_simulation(exe)
    deadline = time() + 30
    while L.cosim_execution_current_time(exe) < L.cosim_to_nanoseconds(2.0) && time() < deadline
        sleep(0.001)
    end
    L.cosim_execution_stop_simulation(exe)
    while L.cosim_execution_is_running(exe)
        sleep(0.001)
    end
    status = L.cosim_execution_get_status(exe)
    @test L.cosim_execution_status_state_get(status) == L.cosim_execution_state_stopped
    @test L.cosim_execution_status_error_code_get(status) == L.cosim_errc_success
    @test L.cosim_execution_status_current_time_get(status) >= L.cosim_to_nanoseconds(2.0)
end

# CF-53 state export and import as bytes (needs an FMU with state export
# and an output "x", e.g. the Dahlquist reference FMU)
if haskey(ENV, "LIBCOSIM_STATE_FMU")
    @testset "Libcosim state" begin
        exe = L.cosim_execution_new(0, L.cosim_fixed_step_algorithm_create(L.cosim_to_nanoseconds(0.01), 0))
        sim = L.cosim_add_fmu(exe, ENV["LIBCOSIM_STATE_FMU"], "dq", 0, C_NULL)
        x = find_ref(exe, sim, "x")
        err = try L.cosim_execution_export_current_state_bytes(exe); nothing catch e; e end
        @test err isa L.CosimError && err.code == L.cosim_errc_invalid_operation

        obs = L.cosim_last_value_observer_new()
        L.cosim_execution_add_observer(exe, obs)
        L.cosim_execution_simulate_until(exe, L.cosim_to_nanoseconds(0.5))
        state = L.bytes(L.cosim_execution_export_current_state_bytes(exe))
        @test state isa Vector{UInt8} && !isempty(state)
        L.cosim_execution_simulate_until(exe, L.cosim_to_nanoseconds(1.0))
        expected = L.cosim_DoubleVector_get(read_one(L.cosim_last_value_observer_get_real, obs, sim, x), 0)

        L.cosim_execution_import_state_bytes(exe, L.byte_vector(state))
        @test L.cosim_execution_current_time(exe) == L.cosim_to_nanoseconds(0.5)
        L.cosim_execution_simulate_until(exe, L.cosim_to_nanoseconds(1.0))
        @test L.cosim_DoubleVector_get(read_one(L.cosim_last_value_observer_get_real, obs, sim, x), 0) == expected

        err = try L.cosim_execution_import_state_bytes(exe, L.byte_vector(UInt8[0xff, 0x00, 0x13])); nothing catch e; e end
        @test err isa L.CosimError && err.code == L.cosim_errc_bad_file
    end
else
    println("state export/import: skipped (LIBCOSIM_STATE_FMU not set)")
end

GC.gc(); GC.gc()  # finalizers release all handles
println("Libcosim Julia package test OK")
