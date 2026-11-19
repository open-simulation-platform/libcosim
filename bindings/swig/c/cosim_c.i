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

// Exceptions declared with 'throws' (e.g. std::out_of_range from the
// container get() functions in SWIG's std_vector.i) are translated by the
// backend's default 'throws' typemaps, which record the error but do NOT
// return, so the wrapper goes on to use an uninitialised result. Override
// them to return immediately.
%typemap(throws, noblock="1") std::out_of_range {
    SWIG_CException_Raise(SWIG_IndexError, $1.what());
    return $null;
}
%typemap(throws, noblock="1") SWIGTYPE {
    SWIG_CException_Raise(SWIG_UnknownError, "exception of type $1_type");
    return $null;
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

%{
#include <cosim/time.hpp>
namespace cosim { namespace swig {
inline long long to_ns(cosim::time_point t) { return t.time_since_epoch().count(); }
inline long long to_ns(cosim::duration d) { return d.count(); }
template<typename T> T from_ns(long long ns);
template<> inline cosim::duration from_ns<cosim::duration>(long long ns) { return cosim::duration(ns); }
template<> inline cosim::time_point from_ns<cosim::time_point>(long long ns) { return cosim::time_point(cosim::duration(ns)); }
}}
%}

%define COSIM_TIME_TYPEMAPS(TYPE)
%typemap(ctype) TYPE, const TYPE& "int64_t"
%typemap(in) TYPE "$1 = cosim::swig::from_ns<TYPE>($input);"
%typemap(in) const TYPE& (TYPE temp) "temp = cosim::swig::from_ns<TYPE>($input); $1 = &temp;"
%typemap(out) TYPE "$result = cosim::swig::to_ns($1);"
%typemap(out) const TYPE& "$result = cosim::swig::to_ns(*$1);"
%enddef

// Fixed-width integers keep their names in the C header (portable across LP64/LLP64).
%typemap(ctype) std::int64_t, int64_t, const std::int64_t&, const int64_t& "int64_t"

COSIM_TIME_TYPEMAPS(cosim::time_point)
COSIM_TIME_TYPEMAPS(cosim::duration)

// For struct members of time type (with %naturalvar, see cosim.i) the C
// backend uses the fully resolved type names rather than the typedefs, so
// the same conversions are registered for those names too.
COSIM_TIME_TYPEMAPS(std::chrono::time_point< cosim::detail::clock >)
%typemap(ctype) std::chrono::duration< cosim::detail::clock::rep, cosim::detail::clock::period >,
    const std::chrono::duration< cosim::detail::clock::rep, cosim::detail::clock::period >& "int64_t"
%typemap(in) std::chrono::duration< cosim::detail::clock::rep, cosim::detail::clock::period >
    "$1 = cosim::swig::from_ns<cosim::duration>($input);"
%typemap(in) const std::chrono::duration< cosim::detail::clock::rep, cosim::detail::clock::period >& (cosim::duration temp)
    "temp = cosim::swig::from_ns<cosim::duration>($input); $1 = &temp;"
%typemap(out) std::chrono::duration< cosim::detail::clock::rep, cosim::detail::clock::period >
    "$result = cosim::swig::to_ns($1);"
%typemap(out) const std::chrono::duration< cosim::detail::clock::rep, cosim::detail::clock::period >&
    "$result = cosim::swig::to_ns(*$1);"

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

// -----------------------------------------------------------------------------
// Ownership. Elements of class type returned from containers
// (cosim_VariableDescriptionVector_get() etc.) are owned copies, like every
// other object this API returns, instead of pointers into the container.
// -----------------------------------------------------------------------------
// (The list of types is in cosim.i.)
%define COSIM_RETURN_COPIES(TYPE)
%typemap(out) const TYPE&, TYPE& "$result = (SwigObj*) new TYPE(*$1);"
%enddef

// The rules, at the top of the generated header.
%insert("cheader") %{
/*
 * libcosim C API (generated by SWIG's C backend).
 *
 * Ownership rules:
 *  - Every object returned by a function is owned by the caller and must be
 *    released with the matching cosim_<type>_delete() function. This includes
 *    elements returned by cosim_<Container>_get() and struct members
 *    returned by cosim_<struct>_get_<member>().
 *  - Every string returned (const char *) is a malloc()ed copy that the
 *    caller must free().
 *  - Scalar members (numbers, enums, booleans) are read and written with
 *    cosim_<struct>_<member>_get() / _set().
 *
 * Errors: a function that fails returns 0/NULL and leaves a pending error.
 * Check it with cosim_last_error_code() (0 = no error, otherwise a
 * cosim_errc value for libcosim errors), read it with
 * cosim_last_error_message() (an owned copy) and clear it with
 * cosim_clear_last_error(). The error stays pending until cleared.
 * (cosim_SWIG_CException_msg_get() is the one function returning a borrowed
 * string; prefer cosim_last_error_message().)
 *
 * Times are int64_t nanoseconds; cosim_to_nanoseconds() and
 * cosim_to_seconds() convert from and to seconds.
 */
%}

%include "../cosim.i"

// -----------------------------------------------------------------------------
// Error access in libcosimc style (after cosim.i, so std::string is known). SWIG's own cosim_SWIG_CException_msg_get()
// returns a pointer into the pending error object (an exception to the
// ownership rules), so these are the recommended way to read errors.
// -----------------------------------------------------------------------------
%inline %{
namespace cosim { namespace swig {
/// Code of the pending error (a cosim_errc value for libcosim errors), or 0 if none.
inline int last_error_code()
{
    const auto e = ::SWIG_CException::get_pending();
    return e ? e->code : 0;
}
/// Message of the pending error, or "" if none. Owned copy: free() it.
inline std::string last_error_message()
{
    const auto e = ::SWIG_CException::get_pending();
    return e ? std::string(e->msg) : std::string();
}
/// Clears the pending error.
inline void clear_last_error()
{
    ::SWIG_CException::reset_pending();
}
}}
%}

