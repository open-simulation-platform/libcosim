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
// Time: cosim::time_point and cosim::duration <-> double seconds.
// -----------------------------------------------------------------------------
%{
#include <cosim/time.hpp>
#include <chrono>
namespace cosim { namespace swig {
inline double duration_to_double(cosim::duration d)
{
    return std::chrono::duration<double>(d).count();
}
}}
%}

%define COSIM_TIME_TYPEMAPS(TYPE, TO_CPP, TO_DOUBLE)
%typemap(ctype) TYPE, const TYPE& "double"
%typemap(in) TYPE "$1 = TO_CPP($input);"
%typemap(in) const TYPE& (TYPE temp) "temp = TO_CPP($input); $1 = &temp;"
%typemap(out) TYPE "$result = TO_DOUBLE($1);"
%enddef

COSIM_TIME_TYPEMAPS(cosim::time_point, cosim::to_time_point, cosim::to_double_time_point)
COSIM_TIME_TYPEMAPS(cosim::duration, cosim::to_duration, cosim::swig::duration_to_double)

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

%include "../cosim.i"
