/**
 *  \file
 *  \brief Defines the `observer` and `observable` interfaces.
 */
#ifndef CSE_OBSERVER_OBSERVER_HPP
#define CSE_OBSERVER_OBSERVER_HPP

#include <cse/execution.hpp>
#include <cse/model.hpp>

#include <string>
#include <string_view>


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
     *  Exposes a variable for retrieval with `get_xxx()`.
     *
     *  The purpose is fundamentally to select which variables get transferred
     *  from remote simulators at each step, so that each individual `get_xxx()`
     *  function call doesn't trigger a separate RPC operation.
     */
    virtual void expose_for_getting(variable_type, value_reference) = 0;

    /**
     *  Returns the value of a real variable.
     *
     *  The variable must previously have been exposed with `expose_for_getting()`.
     */
    virtual double get_real(value_reference) const = 0;

    /**
     *  Returns the value of an integer variable.
     *
     *  The variable must previously have been exposed with `expose_for_getting()`.
     */
    virtual int get_integer(value_reference) const = 0;

    /**
     *  Returns the value of a boolean variable.
     *
     *  The variable must previously have been exposed with `expose_for_getting()`.
     */
    virtual bool get_boolean(value_reference) const = 0;

    /**
     *  Returns the value of a string variable.
     *
     *  The variable must previously have been exposed with `expose_for_getting()`.
     *
     *  The returned `std::string_view` is only guaranteed to remain valid
     *  until the next call of this or any other of the object's methods.
     */
    virtual std::string_view get_string(value_reference) const = 0;

    virtual ~observable() noexcept {}
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
    virtual void simulator_added(simulator_index, observable*, time_point) = 0;

    /// A simulator was removed from the execution.
    virtual void simulator_removed(simulator_index, time_point) = 0;

    /// A variable connection was established.
    virtual void variables_connected(variable_id output, variable_id input, time_point) = 0;

    /// A variable connection was broken.
    virtual void variable_disconnected(variable_id input, time_point) = 0;

    /// A time step is complete, and a communication point was reached.
    virtual void step_complete(
        step_number lastStep,
        duration lastStepSize,
        time_point currentTime) = 0;

    /// A simulator time step is complete, and a communication point was reached.
    virtual void simulator_step_complete(
        simulator_index index,
        step_number lastStep,
        duration lastStepSize,
        time_point currentTime) = 0;

    virtual ~observer() noexcept {}
};


} // namespace cse
#endif // header guard
