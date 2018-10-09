/**
 *  \file
 *  \brief Observer-related functionality.
 */
#ifndef CSE_OBSERVER_HPP
#define CSE_OBSERVER_HPP

#include <string>
#include <string_view>

#include <cse/execution.hpp>
#include <cse/model.hpp>


namespace cse
{


/// Interface for observable entities in a simulation.
class observable
{
public:
    /// Returns the entity's name.
    virtual std::string name() const = 0;

    /// Returns a description of the entity.
    virtual cse::model_description model_description() const = 0;

    /**
     *  Exposes an output variable for retrieval with `get_xxx()`.
     *
     *  The purpose is fundamentally to select which variables get transferred
     *  from remote simulators at each step, so that each individual `get_xxx()`
     *  function call doesn't trigger a separate RPC operation.
     */
    virtual void expose_output(variable_type, variable_index) = 0;

    /**
     *  Returns the value of a real variable.
     *
     *  The variable must previously have been exposed with `expose_output()`.
     */
    virtual double get_real(variable_index) const = 0;

    /**
     *  Returns the value of an integer variable.
     *
     *  The variable must previously have been exposed with `expose_output()`.
     */
    virtual int get_integer(variable_index) const = 0;

    /**
     *  Returns the value of a boolean variable.
     *
     *  The variable must previously have been exposed with `expose_output()`.
     */
    virtual bool get_boolean(variable_index) const = 0;

    /**
     *  Returns the value of a string variable.
     *
     *  The variable must previously have been exposed with `expose_output()`.
     *
     *  The returned `std::string_view` is only guaranteed to remain valid
     *  until the next call of this or any other of the object's methods.
     */
    virtual std::string_view get_string(variable_index) const = 0;

    virtual ~observable() noexcept { }
};


/**
 *  An interface for observers.
 *
 *  The methods in this interface represent various events that the observer
 *  may record or react to in some way. It may query the slaves for variable
 *  values and other info through the `observable` interface at any time.
 */
class observer
{
public:
    /// A simulator was added to the execution.
    virtual void simulator_added(simulator_index, observable*) = 0;

    /// A simulator was removed from the execution.
    virtual void simulator_removed(simulator_index) = 0;

    /// A variable connection was established.
    virtual void variables_connected(variable_id output, variable_id input) = 0;

    /// A variable connection was broken.
    virtual void variable_disconnected(variable_id input) = 0;

    /// A time step is complete, and a communication point was reached.
    virtual void step_complete(
        step_number lastStep,
        time_duration lastStepSize,
        time_point currentTime) = 0;

    virtual ~observer() noexcept { }
};


} // namespace cse
#endif // header guard
