# Julia on top of the SWIG-generated C API (libcosim_c), using plain ccall.
#
# Every object the C API returns is owned by the caller, so each one is
# wrapped in a `Handle` whose finalizer calls the matching *_delete
# function. Handles can be collected in any order: objects shared between
# them (e.g. an observer added to an execution) are reference counted in
# libcosim.
#
# A real package would generate the ccall layer from cosim_c_wrap.h with
# Clang.jl and add an idiomatic API on top of this pattern.
#
# Usage: julia smoke_test.jl <path/to/libcosim_c.so> <OspSystemStructure.xml>

using Libdl

const lib = ARGS[1]
const config = ARGS[2]
const libhandle = Libdl.dlopen(lib)

mutable struct Handle
    ptr::Ptr{Cvoid}
    deleter::Ptr{Cvoid}
end

const deleted = Ref(0)  # number of finalized handles, for the test

# Takes ownership of `ptr`, to be released with the C function `deleter`.
function own(ptr::Ptr{Cvoid}, deleter::Symbol)
    check()
    ptr == C_NULL && error("libcosim returned NULL")
    h = Handle(ptr, Libdl.dlsym(libhandle, deleter))
    finalizer(h) do x
        ccall(x.deleter, Cvoid, (Ptr{Cvoid},), x.ptr)
        deleted[] += 1
    end
end

# Lets a Handle be passed where ccall expects Ptr{Cvoid}; ccall keeps it alive.
Base.unsafe_convert(::Type{Ptr{Cvoid}}, h::Handle) = h.ptr

# Raise a Julia exception if libcosim reported an error.
function check()
    code = ccall((:cosim_last_error_code, lib), Cint, ())
    code == 0 && return
    msgptr = ccall((:cosim_last_error_message, lib), Ptr{Cchar}, ())
    msg = unsafe_string(msgptr)
    Libc.free(msgptr)  # strings returned by the C API are owned copies
    ccall((:cosim_clear_last_error, lib), Cvoid, ())
    error("libcosim error $code: $msg")
end

ns(seconds) = ccall((:cosim_to_nanoseconds, lib), Int64, (Cdouble,), seconds)

function run()
    ccall((:cosim_set_output_level, lib), Cvoid, (Cint,), 3)  # level_warning

    # Error mapping
    try
        own(ccall((:cosim_load_execution_from_ssp, lib), Ptr{Cvoid}, (Cstring, Ptr{Cvoid}),
                  "/nonexistent/system.ssp", C_NULL), :cosim_configured_execution_delete)
        error("expected an error")
    catch e
        println("expected error: ", e.msg)
    end

    le = own(ccall((:cosim_load_execution_from_osp_config, lib), Ptr{Cvoid}, (Cstring, Ptr{Cvoid}),
                   config, C_NULL), :cosim_configured_execution_delete)
    exe = own(ccall((:cosim_configured_execution_get_execution, lib), Ptr{Cvoid}, (Ptr{Cvoid},), le),
              :cosim_execution_delete)
    sims = own(ccall((:cosim_configured_execution_simulators, lib), Ptr{Cvoid}, (Ptr{Cvoid},), le),
               :cosim_StringIntMap_delete)
    sim = ccall((:cosim_StringIntMap_get, lib), Cint, (Ptr{Cvoid}, Cstring), sims, "TrueIdentity"); check()

    # identity.fmu: integerIn / integerOut both have value reference 0
    obs = own(ccall((:cosim_last_value_observer_new, lib), Ptr{Cvoid}, ()), :cosim_last_value_observer_delete)
    man = own(ccall((:cosim_override_manipulator_new, lib), Ptr{Cvoid}, ()), :cosim_override_manipulator_delete)
    ccall((:cosim_execution_add_observer, lib), Cvoid, (Ptr{Cvoid}, Ptr{Cvoid}), exe, obs); check()
    ccall((:cosim_execution_add_manipulator, lib), Cvoid, (Ptr{Cvoid}, Ptr{Cvoid}), exe, man); check()
    ccall((:cosim_override_manipulator_override_integer_variable, lib), Cvoid,
          (Ptr{Cvoid}, Cint, Cuint, Cint), man, sim, 0, 7); check()

    # Times are Int64 nanoseconds, as in libcosimc.
    t0 = ccall((:cosim_execution_current_time, lib), Int64, (Ptr{Cvoid},), exe)
    ok = ccall((:cosim_execution_simulate_until, lib), Bool, (Ptr{Cvoid}, Int64), exe, t0 + ns(0.01)); check()
    t1 = ccall((:cosim_execution_current_time, lib), Int64, (Ptr{Cvoid},), exe)

    refs = own(ccall((:cosim_ValueReferenceVector_new, lib), Ptr{Cvoid}, ()), :cosim_ValueReferenceVector_delete)
    ccall((:cosim_ValueReferenceVector_push_back, lib), Cvoid, (Ptr{Cvoid}, Cuint), refs, 0)
    vals = own(ccall((:cosim_last_value_observer_get_integer, lib), Ptr{Cvoid},
                     (Ptr{Cvoid}, Cint, Ptr{Cvoid}), obs, sim, refs), :cosim_IntVector_delete)
    v = ccall((:cosim_IntVector_get, lib), Cint, (Ptr{Cvoid}, Cint), vals, 0)

    println("simulated $t0 -> $t1 (ok=$ok), integerOut = $v")
    @assert ok && v == 7 && t1 > t0
    # No explicit deletes: all handles go out of scope here and are
    # finalized in whatever order the GC chooses.
end

run()
GC.gc(); GC.gc()
println("finalized handles: ", deleted[])
@assert deleted[] == 7  # le, exe, sims, obs, man, refs, vals
println("Julia smoke test OK")
