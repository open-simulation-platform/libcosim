# Julia on top of the SWIG-generated C API (libcosim_c), using plain ccall.
# A real package would generate these wrappers from cosim_c_wrap.h with
# Clang.jl and add an idiomatic layer (finalizers, Dict, exceptions).
#
# Usage: julia smoke_test.jl <path/to/libcosim_c.so> <OspSystemStructure.xml>

const lib = ARGS[1]
const config = ARGS[2]

# Raise a Julia exception if libcosim reported an error.
function check()
    ex = ccall((:cosim_SWIG_CException_get_pending, lib), Ptr{Cvoid}, ())
    ex == C_NULL && return
    code = ccall((:cosim_SWIG_CException_code_get, lib), Cint, (Ptr{Cvoid},), ex)
    msg = unsafe_string(ccall((:cosim_SWIG_CException_msg_get, lib), Cstring, (Ptr{Cvoid},), ex))
    ccall((:cosim_SWIG_CException_reset_pending, lib), Cvoid, ())
    error("libcosim error $code: $msg")
end

ccall((:cosim_set_output_level, lib), Cvoid, (Cint,), 3)  # level_warning

# Error mapping
try
    ccall((:cosim_load_execution_from_ssp, lib), Ptr{Cvoid}, (Cstring, Ptr{Cvoid}), "/nonexistent/system.ssp", C_NULL); check()
    error("expected an error")
catch e
    println("expected error: ", e.msg)
end

le = ccall((:cosim_load_execution_from_osp_config, lib), Ptr{Cvoid}, (Cstring, Ptr{Cvoid}), config, C_NULL); check()
exe = ccall((:cosim_configured_execution_execution_get, lib), Ptr{Cvoid}, (Ptr{Cvoid},), le)
sims = ccall((:cosim_configured_execution_simulators, lib), Ptr{Cvoid}, (Ptr{Cvoid},), le)
sim = ccall((:cosim_StringIntMap_get, lib), Cint, (Ptr{Cvoid}, Cstring), sims, "TrueIdentity"); check()

# identity.fmu: integerIn / integerOut both have value reference 0
obs = ccall((:cosim_last_value_observer_new, lib), Ptr{Cvoid}, ())
man = ccall((:cosim_override_manipulator_new, lib), Ptr{Cvoid}, ())
ccall((:cosim_execution_add_observer, lib), Cvoid, (Ptr{Cvoid}, Ptr{Cvoid}), exe, obs); check()
ccall((:cosim_execution_add_manipulator, lib), Cvoid, (Ptr{Cvoid}, Ptr{Cvoid}), exe, man); check()
ccall((:cosim_override_manipulator_override_integer_variable, lib), Cvoid,
      (Ptr{Cvoid}, Cint, Cuint, Cint), man, sim, 0, 7); check()

# Times are Int64 nanoseconds, as in libcosimc.
t0 = ccall((:cosim_execution_current_time, lib), Int64, (Ptr{Cvoid},), exe)
ok = ccall((:cosim_execution_simulate_until, lib), Bool, (Ptr{Cvoid}, Int64), exe, t0 + 10_000_000); check()
t1 = ccall((:cosim_execution_current_time, lib), Int64, (Ptr{Cvoid},), exe)

refs = ccall((:cosim_ValueReferenceVector_new, lib), Ptr{Cvoid}, ())
ccall((:cosim_ValueReferenceVector_push_back, lib), Cvoid, (Ptr{Cvoid}, Cuint), refs, 0)
vals = ccall((:cosim_last_value_observer_get_integer, lib), Ptr{Cvoid},
             (Ptr{Cvoid}, Cint, Ptr{Cvoid}), obs, sim, refs); check()
v = ccall((:cosim_IntVector_get, lib), Cint, (Ptr{Cvoid}, Cint), vals, 0)

println("simulated $t0 -> $t1 (ok=$ok), integerOut = $v")
@assert ok && v == 7 && t1 > t0

# ccall needs literal function names, so cleanup is spelled out.
ccall((:cosim_IntVector_delete, lib), Cvoid, (Ptr{Cvoid},), vals)
ccall((:cosim_ValueReferenceVector_delete, lib), Cvoid, (Ptr{Cvoid},), refs)
ccall((:cosim_override_manipulator_delete, lib), Cvoid, (Ptr{Cvoid},), man)
ccall((:cosim_last_value_observer_delete, lib), Cvoid, (Ptr{Cvoid},), obs)
ccall((:cosim_StringIntMap_delete, lib), Cvoid, (Ptr{Cvoid},), sims)
# `exe` is borrowed from `le` (member getter), so it is not deleted here.
ccall((:cosim_configured_execution_delete, lib), Cvoid, (Ptr{Cvoid},), le)
println("Julia smoke test OK")
