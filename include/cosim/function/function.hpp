/**
 *  \file
 *  The `function` API.
 */
#ifndef COSIM_FUNCTION_FUNCTION_HPP
#define COSIM_FUNCTION_FUNCTION_HPP

#include <cosim/function/description.hpp>

#include <memory>
#include <unordered_map>


namespace cosim
{


/**
 *  An interface for function instances.
 *
 *  ### Functions
 *
 *  In the context of this library, a "function" is some operation that is
 *  performed on variables between co-simulation time steps, i.e., at
 *  synchronisation points.
 *
 *  A function has a set of input and output variables which can be
 *  connected to compatible simulator input and output variables.
 *  It thus becomes part of the overall connection graph of the system
 *  in much the same way as simulators.  As an example, consider the
 *  following system:
 *
 *      .----.
 *      | S1 |---.   .---.
 *      '----'   '-->|   |       .----.
 *                   | F |------>| S3 |
 *      .----.   .-->|   |       '----'
 *      | S2 |---'   '---'
 *      '----'
 *
 *  Here, `F` is a function that has two input variables, connected to the
 *  output variables of simulators `S1` and `S2`, and one output variable,
 *  connected to an input variable of simulator `S3`.  `F` could for example
 *  represent a summation operation.
 *
 *  The principal difference between a function and a simulator is that
 *  no (simulated) time passes during the evaluation of a function.
 *  A simulation of the above system through synchronisation points
 *  `t0, t1, ...` would thus proceed as follows:
 *
 *   1. Advance `S1`, `S2` and `S3` from time `t0` to time `t1`
 *   2. Calculate `F`
 *   3. Advance `S1`, `S2` and `S3` from time `t1` to time `t2`
 *   4. Calculate `F`
 *   5. ...and so on
 *
 *  Another important difference is that, while a simulator has a flat,
 *  static set of variables, a function's variables are organised in groups,
 *  and the number and properties of the variables can vary dynamically.
 *  In particular, the following properties may be defined as run-time
 *  parameters:
 *
 *    * The number of instances of a variable group
 *    * The number of instances of a variable
 *    * The type of a variable
 *
 *  This enables one to define versatile, generic function types that can be
 *  used in a wide range of situations.
 *
 *  As an example, we could define a "vector sum" function that
 *  accepts an arbitrary number of input vectors (group instances) of
 *  arbitrary dimension (variable instances), supporting both real and
 *  integer vectors (variable type).  See `vector_sum_function_type` for a
 *  concrete implementation of this.
 *
 *  ### Function types and function instances
 *
 *  To be more precise, we need to distinguish between *function types* and
 *  *function instances*.  The distinction is analogue to that between a
 *  class and an object in object-oriented programming, or – perhaps more
 *  relevantly – between a *model* and a *simulator* in this library.
 *  Above, we've sloppily referred to both as "functions", but the difference
 *  is crucial.
 *
 *  A function *type* defines the function in an abstract way.  This
 *  includes which parameters it has, which input/output variables it has
 *  and how they depend on the parameters, and so on.  Function types
 *  are represented in code by the `function_type` interface.
 *
 *  Given a function type and a set of concrete parameter values, you can
 *  create a function *instance*.  This the entity that you add to an
 *  execution and which performs the actual calculations during a simulation.
 *  You cannot change the parameter values of a function instance, only
 *  its input variable values.  Function instances are represented in code
 *  by the `function` interface, and are typically created with
 *  `function_type::instantiate()`.
 */
class function
{
public:
    /**
     *  Returns a description of the function instance.
     *
     *  The returned object may not contain any `function_parameter_placeholder`
     *  values, since all parameters must have a specific value once the
     *  function has been instantiated.
     */
    virtual function_description description() const = 0;

    /// Sets the value of a real input variable.
    virtual void set_real(const function_io_reference& reference, double value) = 0;

    /// Sets the value of an integer input variable.
    virtual void set_integer(const function_io_reference& reference, int value) = 0;

    /// Sets the value of a boolean input variable.
    virtual void set_boolean(const function_io_reference& reference, bool value) = 0;

    /// Sets the value of a boolean input variable.
    virtual void set_string(const function_io_reference& reference, std::string_view value) = 0;

    /**
     *  Retrieves the value of a real variable.
     *
     *  If `reference` refers to an output variable, the function is only
     *  required to return a valid value *after* `calculate()` has been
     *  called, and *before* the next call to any of the `set_xxx()`
     *  functions.
     */
    virtual double get_real(const function_io_reference& reference) const = 0;

    /**
     *  Retrieves the value of an integer variable.
     *
     *  If `reference` refers to an output variable, the function is only
     *  required to return a valid value *after* `calculate()` has been
     *  called, and *before* the next call to any of the `set_xxx()`
     *  functions.
     */
    virtual int get_integer(const function_io_reference& reference) const = 0;

    /**
     *  Retrieves the value of a boolean variable.
     *
     *  If `reference` refers to an output variable, the function is only
     *  required to return a valid value *after* `calculate()` has been
     *  called, and *before* the next call to any of the `set_xxx()`
     *  functions.
     */
    virtual bool get_boolean(const function_io_reference& reference) const = 0;

    /**
     *  Retrieves the value of a real variable.
     *
     *  If `reference` refers to an output variable, the function is only
     *  required to return a valid value *after* `calculate()` has been
     *  called, and *before* the next call to any of the `set_xxx()`
     *  functions.
     *
     *  The returned string view must remain valid at least until the next
     *  time any of the functions in this interface are called.
     */
    virtual std::string_view get_string(const function_io_reference& reference) const = 0;

    /// Performs the function calculations.
    virtual void calculate() = 0;
};


/**
 *  Interface for classes that represent function types and act as
 *  factory classes for `function` instances.
 *
 *  For more information about function types and instances, see the
 *  documentation for `cosim::function`.
 */
class function_type
{
public:
    /**
     *  Returns a description of the function type.
     *
     *  Some aspects of a function description may depend on the values of
     *  certain parameters, which are only known after instantiation.  Such
     *  fields are given a `function_parameter_placeholder` value in the
     *  returned object.
     */
    virtual function_type_description description() const = 0;

    /**
     *  Instantiates a function of this type.
     *
     *  \param parameters
     *      The set of parameter values with which the function will be
     *      instantiated.  The keys in this map are the parameters' positions
     *      (indexes) in the `function_type_description::parameters` vector
     *      returned by `description()`.
     */
    virtual std::unique_ptr<function> instantiate(
        const function_parameter_value_map& parameters) = 0;
};


} // namespace cosim
#endif // header guard
