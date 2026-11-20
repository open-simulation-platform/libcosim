// Python-specific part of the libcosim SWIG bindings.
%module(package="libcosim") cosim

%include <exception.i>

// -----------------------------------------------------------------------------
// Errors: cosim::error -> libcosim.CosimError (carries the cosim::errc code),
// other std::exception -> RuntimeError.
// -----------------------------------------------------------------------------
%{
static PyObject* g_cosimErrorType = nullptr;
%}

%init %{
    g_cosimErrorType = PyErr_NewException("libcosim.CosimError", PyExc_RuntimeError, nullptr);
    Py_INCREF(g_cosimErrorType);
    PyModule_AddObject(m, "CosimError", g_cosimErrorType);
%}

%exception {
    try {
        $action
    } catch (const cosim::error& e) {
        PyObject* args = Py_BuildValue("(si)", e.what(), e.code().value());
        PyErr_SetObject(g_cosimErrorType, args);
        Py_XDECREF(args);
        SWIG_fail;
    } catch (const std::exception& e) {
        SWIG_exception(SWIG_RuntimeError, e.what());
    }
}

// -----------------------------------------------------------------------------
// Time: cosim::time_point and cosim::duration <-> int nanoseconds, as in
// libcosimc and libcosimpy. Floats are rejected rather than truncated, since
// a float is almost always meant as seconds: use libcosim.to_nanoseconds().
// -----------------------------------------------------------------------------
%{
#include <cosim/time.hpp>
namespace cosim { namespace swig {
inline bool py_to_nanoseconds(PyObject* obj, long long& out)
{
    if (PyFloat_Check(obj)) {
        PyErr_SetString(PyExc_TypeError,
            "times are integer nanoseconds; use libcosim.to_nanoseconds(seconds) to convert from seconds");
        return false;
    }
    if (!PyIndex_Check(obj)) {
        PyErr_SetString(PyExc_TypeError, "expected an int (nanoseconds)");
        return false;
    }
    out = PyLong_AsLongLong(obj);
    return !(out == -1 && PyErr_Occurred());
}
}}
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
%typemap(in) TYPE {
    long long ns = 0;
    if (!cosim::swig::py_to_nanoseconds($input, ns)) SWIG_fail;
    $1 = cosim::swig::from_ns<TYPE>(ns);
}
%typemap(in) const TYPE& (TYPE temp) {
    long long ns = 0;
    if (!cosim::swig::py_to_nanoseconds($input, ns)) SWIG_fail;
    temp = cosim::swig::from_ns<TYPE>(ns);
    $1 = &temp;
}
%typemap(out) TYPE "$result = PyLong_FromLongLong(cosim::swig::to_ns($1));"
%typemap(out) const TYPE& "$result = PyLong_FromLongLong(cosim::swig::to_ns(*$1));"
%typemap(typecheck, precedence=SWIG_TYPECHECK_INT64) TYPE, const TYPE& {
    $1 = (PyIndex_Check($input) && !PyFloat_Check($input)) ? 1 : 0;
}
%enddef

COSIM_TIME_TYPEMAPS(cosim::time_point)
COSIM_TIME_TYPEMAPS(cosim::duration)

// -----------------------------------------------------------------------------
// Paths: cosim::filesystem::path <-> str (accepts str or any os.PathLike).
// -----------------------------------------------------------------------------
%{
#include <cosim/fs_portability.hpp>
namespace cosim { namespace swig {
inline bool to_path(PyObject* obj, cosim::filesystem::path& out)
{
    PyObject* fs = PyOS_FSPath(obj);
    if (!fs) return false;
    const char* utf8 = PyUnicode_Check(fs) ? PyUnicode_AsUTF8AndSize(fs, nullptr) : nullptr; // limited API (>= 3.10)
    if (utf8) out = cosim::filesystem::u8path(utf8);
    Py_DECREF(fs);
    if (!utf8 && !PyErr_Occurred()) PyErr_SetString(PyExc_TypeError, "expected str or os.PathLike");
    return utf8 != nullptr;
}
}}
%}
%typemap(in) cosim::filesystem::path {
    if (!cosim::swig::to_path($input, $1)) SWIG_fail;
}
%typemap(in) const cosim::filesystem::path& (cosim::filesystem::path temp) {
    if (!cosim::swig::to_path($input, temp)) SWIG_fail;
    $1 = &temp;
}
%typemap(out) cosim::filesystem::path {
    $result = PyUnicode_FromString(reinterpret_cast<const char*>($1.u8string().c_str()));
}
%typemap(typecheck, precedence=SWIG_TYPECHECK_STRING) cosim::filesystem::path, const cosim::filesystem::path& {
    $1 = (PyUnicode_Check($input) || PyObject_HasAttrString($input, "__fspath__")) ? 1 : 0;
}

// -----------------------------------------------------------------------------
// Ownership: references to objects of these types (struct members accessed
// via %naturalvar, container elements) are returned as owned copies, so a
// Python object never points into another object that may already be gone
// (e.g. `exe.get_model_description(i).variables`). The list of types is in
// cosim.i.
// -----------------------------------------------------------------------------
%define COSIM_RETURN_COPIES(TYPE)
%typemap(out) const TYPE&, TYPE&
    "$result = SWIG_NewPointerObj(new TYPE(*$1), $descriptor(TYPE*), SWIG_POINTER_OWN);"
%enddef

%include "../cosim.i"
