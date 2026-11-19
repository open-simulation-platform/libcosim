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

GC.gc(); GC.gc()  # finalizers release all handles
println("Libcosim Julia package test OK")
