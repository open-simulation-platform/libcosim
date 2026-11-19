// C#-specific part of the libcosim SWIG bindings (spike).
// Generated classes go in namespace `Libcosim`; the native library is
// `cosim_csharp` (DllImport).
%module cosim

// -----------------------------------------------------------------------------
// Errors: cosim::error -> Libcosim.CosimException (carries the cosim::errc
// code), other std::exception -> System.ApplicationException.
// C# exceptions are raised through a callback registered by the generated
// intermediate class, following the pattern in SWIG's C# manual.
// -----------------------------------------------------------------------------
%insert(runtime) %{
typedef void (SWIGSTDCALL* CosimExceptionCallback_t)(const char*, int);
static CosimExceptionCallback_t cosim_exception_callback = NULL;

extern "C" SWIGEXPORT void SWIGSTDCALL CosimRegisterExceptionCallback_cosim(CosimExceptionCallback_t callback)
{
    cosim_exception_callback = callback;
}

static void cosim_set_pending_exception(const char* message, int code)
{
    cosim_exception_callback(message, code);
}
%}

%pragma(csharp) imclasscode=%{
  class CosimExceptionHelper {
    public delegate void CosimExceptionDelegate(string message, int code);
    static CosimExceptionDelegate cosimDelegate = new CosimExceptionDelegate(SetPendingCosimException);

    [global::System.Runtime.InteropServices.DllImport("$dllimport", EntryPoint="CosimRegisterExceptionCallback_cosim")]
    public static extern void CosimRegisterExceptionCallback(CosimExceptionDelegate callback);

    static void SetPendingCosimException(string message, int code) {
      SWIGPendingException.Set(new Libcosim.CosimException(message, code));
    }

    static CosimExceptionHelper() {
      CosimRegisterExceptionCallback(cosimDelegate);
    }
  }
  static CosimExceptionHelper cosimExceptionHelper = new CosimExceptionHelper();
%}

%exception {
    try {
        $action
    } catch (const cosim::error& e) {
        cosim_set_pending_exception(e.what(), e.code().value());
        return $null;
    } catch (const std::exception& e) {
        SWIG_CSharpSetPendingException(SWIG_CSharpApplicationException, e.what());
        return $null;
    }
}

// -----------------------------------------------------------------------------
// Time: cosim::time_point and cosim::duration <-> long nanoseconds.
// -----------------------------------------------------------------------------
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
%typemap(ctype) TYPE, const TYPE& "long long"
%typemap(imtype) TYPE, const TYPE& "long"
%typemap(cstype) TYPE, const TYPE& "long"
%typemap(csin) TYPE, const TYPE& "$csinput"
%typemap(csout, excode=SWIGEXCODE) TYPE, const TYPE& {
    long ret = $imcall;$excode
    return ret;
  }
%typemap(csvarin, excode=SWIGEXCODE2) TYPE, const TYPE& %{
    set {
      $imcall;$excode
    } %}
%typemap(csvarout, excode=SWIGEXCODE2) TYPE, const TYPE& %{
    get {
      long ret = $imcall;$excode
      return ret;
    } %}
%typemap(in) TYPE "$1 = cosim::swig::from_ns<TYPE>($input);"
%typemap(in) const TYPE& (TYPE temp) "temp = cosim::swig::from_ns<TYPE>($input); $1 = &temp;"
%typemap(out) TYPE "$result = cosim::swig::to_ns($1);"
%typemap(out) const TYPE& "$result = cosim::swig::to_ns(*$1);"
%typemap(typecheck, precedence=SWIG_TYPECHECK_INT64) TYPE, const TYPE& ""
%enddef

COSIM_TIME_TYPEMAPS(cosim::time_point)
COSIM_TIME_TYPEMAPS(cosim::duration)

// -----------------------------------------------------------------------------
// Paths: cosim::filesystem::path <-> string.
// -----------------------------------------------------------------------------
%{
#include <cosim/fs_portability.hpp>
%}
%typemap(ctype) cosim::filesystem::path, const cosim::filesystem::path& "char *"
%typemap(imtype) cosim::filesystem::path, const cosim::filesystem::path& "string"
%typemap(cstype) cosim::filesystem::path, const cosim::filesystem::path& "string"
%typemap(csin) cosim::filesystem::path, const cosim::filesystem::path& "$csinput"
%typemap(csout, excode=SWIGEXCODE) cosim::filesystem::path {
    string ret = $imcall;$excode
    return ret;
  }
%typemap(in, canthrow=1) cosim::filesystem::path %{
    if (!$input) {
        SWIG_CSharpSetPendingExceptionArgument(SWIG_CSharpArgumentNullException, "null path", 0);
        return $null;
    }
    $1 = cosim::filesystem::u8path($input);
%}
%typemap(in, canthrow=1) const cosim::filesystem::path& (cosim::filesystem::path temp) %{
    if (!$input) {
        SWIG_CSharpSetPendingExceptionArgument(SWIG_CSharpArgumentNullException, "null path", 0);
        return $null;
    }
    temp = cosim::filesystem::u8path($input);
    $1 = &temp;
%}
%typemap(out) cosim::filesystem::path
    "$result = SWIG_csharp_string_callback(reinterpret_cast<const char*>($1.u8string().c_str()));"
%typemap(typecheck, precedence=SWIG_TYPECHECK_STRING) cosim::filesystem::path, const cosim::filesystem::path& ""

// -----------------------------------------------------------------------------
// Ownership: references to these types are returned as owned copies (C#
// proxies that own and later delete their C++ object). List in cosim.i.
// -----------------------------------------------------------------------------
%define COSIM_RETURN_COPIES(TYPE)
%typemap(out) const TYPE&, TYPE& "$result = (void*) new TYPE(*$1);"
%typemap(csout, excode=SWIGEXCODE) const TYPE&, TYPE& {
    $csclassname ret = new $csclassname($imcall, true);$excode
    return ret;
  }
%typemap(csvarout, excode=SWIGEXCODE2) const TYPE&, TYPE& %{
    get {
      $csclassname ret = new $csclassname($imcall, true);$excode
      return ret;
    } %}
%enddef

%include "../cosim.i"
