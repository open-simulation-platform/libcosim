// Java-specific part of the libcosim SWIG bindings.
// Generated classes go in package `libcosim`; the native library is
// `cosim_java` (System.loadLibrary("cosim_java")).
%module cosim

// C++ enums become proper Java enums (SWIG's default is "typesafe enum"
// classes), so they work with switch, EnumSet and EnumMap.
%include "enums.swg"

// Load the native libraries (bundled in the JAR, or from java.library.path)
// when the JNI class is first used.
%pragma(java) jniclasscode=%{
  static {
    libcosim.NativeLoader.load();
  }
%}

// -----------------------------------------------------------------------------
// Errors: cosim::error -> libcosim.CosimException (carries the cosim::errc
// code), other std::exception -> java.lang.RuntimeException.
// -----------------------------------------------------------------------------
%{
static void cosim_throw_java(JNIEnv* jenv, int code, const char* message)
{
    jclass cls = jenv->FindClass("libcosim/CosimException");
    if (!cls) return; // ClassNotFoundError is now pending
    jmethodID ctor = jenv->GetMethodID(cls, "<init>", "(Ljava/lang/String;I)V");
    jstring jmessage = jenv->NewStringUTF(message);
    jobject exception = jenv->NewObject(cls, ctor, jmessage, (jint)code);
    if (exception) jenv->Throw((jthrowable)exception);
}
%}

%exception {
    try {
        $action
    } catch (const cosim::error& e) {
        cosim_throw_java(jenv, e.code().value(), e.what());
        return $null;
    } catch (const std::exception& e) {
        SWIG_JavaThrowException(jenv, SWIG_JavaRuntimeException, e.what());
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
%typemap(jni) TYPE, const TYPE& "jlong"
%typemap(jtype) TYPE, const TYPE& "long"
%typemap(jstype) TYPE, const TYPE& "long"
%typemap(javain) TYPE, const TYPE& "$javainput"
%typemap(javaout) TYPE, const TYPE& { return $jnicall; }
%typemap(in) TYPE "$1 = cosim::swig::from_ns<TYPE>($input);"
%typemap(in) const TYPE& (TYPE temp) "temp = cosim::swig::from_ns<TYPE>($input); $1 = &temp;"
%typemap(out) TYPE "$result = cosim::swig::to_ns($1);"
%typemap(out) const TYPE& "$result = cosim::swig::to_ns(*$1);"
%typemap(typecheck, precedence=SWIG_TYPECHECK_INT64) TYPE, const TYPE& ""
%enddef

COSIM_TIME_TYPEMAPS(cosim::time_point)
COSIM_TIME_TYPEMAPS(cosim::duration)

// -----------------------------------------------------------------------------
// Paths: cosim::filesystem::path <-> String.
// -----------------------------------------------------------------------------
%{
#include <cosim/fs_portability.hpp>
namespace cosim { namespace swig {
inline bool java_to_path(JNIEnv* jenv, jstring s, cosim::filesystem::path& out)
{
    if (!s) {
        SWIG_JavaThrowException(jenv, SWIG_JavaNullPointerException, "null path");
        return false;
    }
    const char* chars = jenv->GetStringUTFChars(s, nullptr);
    if (!chars) return false;
    out = cosim::filesystem::u8path(chars);
    jenv->ReleaseStringUTFChars(s, chars);
    return true;
}
}}
%}
%typemap(jni) cosim::filesystem::path, const cosim::filesystem::path& "jstring"
%typemap(jtype) cosim::filesystem::path, const cosim::filesystem::path& "String"
%typemap(jstype) cosim::filesystem::path, const cosim::filesystem::path& "String"
%typemap(javain) cosim::filesystem::path, const cosim::filesystem::path& "$javainput"
%typemap(javaout) cosim::filesystem::path { return $jnicall; }
%typemap(in) cosim::filesystem::path {
    if (!cosim::swig::java_to_path(jenv, $input, $1)) return $null;
}
%typemap(in) const cosim::filesystem::path& (cosim::filesystem::path temp) {
    if (!cosim::swig::java_to_path(jenv, $input, temp)) return $null;
    $1 = &temp;
}
%typemap(out) cosim::filesystem::path
    "$result = jenv->NewStringUTF(reinterpret_cast<const char*>($1.u8string().c_str()));"
%typemap(typecheck, precedence=SWIG_TYPECHECK_STRING) cosim::filesystem::path, const cosim::filesystem::path& ""

// -----------------------------------------------------------------------------
// Ownership: references to these types are returned as owned copies (Java
// proxies that own and later delete their C++ object). List in cosim.i.
// -----------------------------------------------------------------------------
%define COSIM_RETURN_COPIES(TYPE)
%typemap(out) const TYPE&, TYPE& "*(TYPE **)&$result = new TYPE(*$1);"
%typemap(javaout) const TYPE&, TYPE& { return new $javaclassname($jnicall, true); }
%enddef

%include "../cosim.i"
