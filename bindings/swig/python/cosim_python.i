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

%include "../cosim.i"
