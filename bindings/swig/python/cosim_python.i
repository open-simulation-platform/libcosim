// Python-specific part of the libcosim SWIG bindings (spike).
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
// Time: cosim::time_point and cosim::duration <-> float seconds.
// -----------------------------------------------------------------------------
%define COSIM_TIME_TYPEMAPS(TYPE, TO_CPP, TO_DOUBLE)
%typemap(in) TYPE {
    const double d = PyFloat_AsDouble($input);
    if (d == -1.0 && PyErr_Occurred()) SWIG_fail;
    $1 = TO_CPP(d);
}
%typemap(in) const TYPE& (TYPE temp) {
    const double d = PyFloat_AsDouble($input);
    if (d == -1.0 && PyErr_Occurred()) SWIG_fail;
    temp = TO_CPP(d);
    $1 = &temp;
}
%typemap(out) TYPE {
    $result = PyFloat_FromDouble(TO_DOUBLE($1));
}
%typemap(typecheck, precedence=SWIG_TYPECHECK_DOUBLE) TYPE, const TYPE& {
    $1 = (PyFloat_Check($input) || PyLong_Check($input)) ? 1 : 0;
}
%enddef

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

COSIM_TIME_TYPEMAPS(cosim::time_point, cosim::to_time_point, cosim::to_double_time_point)
COSIM_TIME_TYPEMAPS(cosim::duration, cosim::to_duration, cosim::swig::duration_to_double)

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
    const char* utf8 = PyUnicode_Check(fs) ? PyUnicode_AsUTF8(fs) : nullptr;
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

%include "../cosim.i"
