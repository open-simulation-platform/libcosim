/**
 *  \file
 *  The `function` API.
 */
#ifndef CSE_FUNCTION_FUNCTION_HPP
#define CSE_FUNCTION_FUNCTION_HPP

#include <cse/function/description.hpp>

#include <memory>
#include <unordered_map>


namespace cse
{


/// An interface for function instances.
class function
{
public:
    /**
     *  Returns a description of the function.
     *
     *  The returned object may not contain any `function_parameter_placeholder`
     *  values, since all parameters must have a specific value once the
     *  function has been instantiated.
     */
    virtual function_type_description description() const = 0;

    /// Sets the value of a real-valued variable.
    virtual void set_real_io(const function_io_reference& reference, double value) = 0;

    /// Sets the value of an integer-valued variable.
    virtual void set_integer_io( const function_io_reference& reference, int value) = 0;

    /// Retrieves the value of a real-valued variable.
    virtual double get_real_io(const function_io_reference& reference) const = 0;

    /// Retrieves the value of an integer-valued variable.
    virtual int get_integer_io(const function_io_reference& reference) const = 0;

    /// Performs the function calculations.
    virtual void calculate() = 0;
};


/**
 *  An interface for function type descriptors.
 *
 *  Classes that implement this interface act as factory classes for `function`
 *  instances.
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
        const std::unordered_map<int, function_parameter_value> parameters) = 0;
};


} // namespace cse
#endif // header guard
