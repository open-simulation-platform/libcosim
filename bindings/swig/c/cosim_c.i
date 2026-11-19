// C-specific part of the libcosim SWIG bindings (spike), using SWIG's
// experimental C backend (SWIG >= 4.3).
//
%module cosim

// -----------------------------------------------------------------------------
// Errors. The C backend only translates exceptions declared via 'throws'
// typemaps; without this catch-all any libcosim exception would call
// std::terminate in the caller's process. Errors become a pending
// SWIG_CException (code = cosim::errc value for cosim::error) that the caller
// checks with cosim_SWIG_CException_get_pending().
// -----------------------------------------------------------------------------
%exception {
    try {
        $action
    } catch (const cosim::error& e) {
        SWIG_CException_Raise(e.code().value(), e.what());
        return $null;
    } catch (const std::exception& e) {
        SWIG_CException_Raise(SWIG_RuntimeError, e.what());
        return $null;
    } catch (...) {
        SWIG_CException_Raise(SWIG_UnknownError, "unknown C++ exception");
        return $null;
    }
}

// -----------------------------------------------------------------------------
// Time: cosim::time_point and cosim::duration <-> int64_t nanoseconds, as
// cosim_time_point / cosim_duration in libcosimc. Seconds helpers:
// cosim_to_nanoseconds() and cosim_to_seconds().
// -----------------------------------------------------------------------------
%{
#include <cosim/time.hpp>
%}
%insert("cheader") %{
#include <stdint.h>
%}

%define COSIM_TIME_TYPEMAPS(TYPE, FROM_NS, TO_NS)
%typemap(ctype) TYPE, const TYPE& "int64_t"
%typemap(in) TYPE "$1 = FROM_NS;"
%typemap(in) const TYPE& (TYPE temp) "temp = FROM_NS; $1 = &temp;"
%typemap(out) TYPE "$result = TO_NS;"
%enddef

// Fixed-width integers keep their names in the C header (portable across LP64/LLP64).
%typemap(ctype) std::int64_t, int64_t, const std::int64_t&, const int64_t& "int64_t"

COSIM_TIME_TYPEMAPS(cosim::time_point, cosim::time_point(cosim::duration($input)), ($1).time_since_epoch().count())
COSIM_TIME_TYPEMAPS(cosim::duration, cosim::duration($input), ($1).count())

// -----------------------------------------------------------------------------
// Paths: cosim::filesystem::path <-> UTF-8 const char*. Returned strings are
// malloc()ed copies the caller must free(), like all strings from this API.
// -----------------------------------------------------------------------------
%{
#include <cosim/fs_portability.hpp>
#include <cstring>
%}
%typemap(ctype) cosim::filesystem::path, const cosim::filesystem::path& "const char *"
%typemap(in) cosim::filesystem::path "$1 = cosim::filesystem::u8path($input ? $input : \"\");"
%typemap(in) const cosim::filesystem::path& (cosim::filesystem::path temp)
    "temp = cosim::filesystem::u8path($input ? $input : \"\"); $1 = &temp;"
%typemap(out) cosim::filesystem::path "$result = strdup(reinterpret_cast<const char*>($1.u8string().c_str()));"

// -----------------------------------------------------------------------------
// Loader options: NULL means default options.
// -----------------------------------------------------------------------------
%typemap(in) const cosim::execution_load_options& (cosim::execution_load_options defaults)
    "$1 = $input ? (cosim::execution_load_options*)$input : &defaults;"

%include "../cosim.i"
