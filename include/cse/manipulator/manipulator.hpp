/**
 *  \file
 *  \brief Defines the `manipulator` and `manipulable` interfaces.
 */

#ifndef CSECORE_MANIPULATOR_MANIPULATOR_HPP
#define CSECORE_MANIPULATOR_MANIPULATOR_HPP

#include <cse/execution.hpp>
#include <cse/model.hpp>
#include <cse/observer/observer.hpp>

#include <functional>
#include <string>

namespace cse
{

/// Interface for manipulable entities in a simulation.
class manipulable : public observable
{
public:
    /**
     *  Exposes a variable for assignment with `set_xxx()`.
     *
     *  The purpose is fundamentally to select which variables get transferred
     *  to remote simulators at each step, so that each individual `set_xxx()`
     *  function call doesn't trigger a new data exchange.
     *
     *  Calling this function more than once for the same variable has no
     *  effect.
     */
    virtual void expose_for_setting(variable_type, value_reference) = 0;

    /**
     *  Sets a modifier for the value of a real input variable.
     *
     *  The variable must previously have been exposed with `expose_for_setting()`.
     */
    virtual void set_real_input_modifier(
        value_reference reference,
        std::function<double(double)> modifier) = 0;

    /**
     *  Sets a modifier for the value of an integer input variable.
     *
     *  The variable must previously have been exposed with `expose_for_setting()`.
     */
    virtual void set_integer_input_modifier(
        value_reference reference,
        std::function<int(int)> modifier) = 0;

    /**
     *  Sets a modifier for the value of a boolean input variable.
     *
     *  The variable must previously have been exposed with `expose_for_setting()`.
     */
    virtual void set_boolean_input_modifier(
        value_reference reference,
        std::function<bool(bool)> modifier) = 0;

    /**
     *  Sets a modifier for the value of a string input variable.
     *
     *  The variable must previously have been exposed with `expose_for_setting()`.
     */
    virtual void set_string_input_modifier(
        value_reference reference,
        std::function<std::string(std::string_view)> modifier) = 0;

    /**
     *  Sets a modifier for the value of a real output variable.
     *
     *  The variable must previously have been exposed with `expose_for_setting()`.
     */
    virtual void set_real_output_modifier(
        value_reference reference,
        std::function<double(double)> modifier) = 0;

    /**
     *  Sets a modifier for the value of an integer output variable.
     *
     *  The variable must previously have been exposed with `expose_for_setting()`.
     */
    virtual void set_integer_output_modifier(
        value_reference reference,
        std::function<int(int)> modifier) = 0;

    /**
     *  Sets a modifier for the value of a boolean output variable.
     *
     *  The variable must previously have been exposed with `expose_for_setting()`.
     */
    virtual void set_boolean_output_modifier(
        value_reference reference,
        std::function<bool(bool)> modifier) = 0;

    /**
     *  Sets a modifier for the value of a string output variable.
     *
     *  The variable must previously have been exposed with `expose_for_setting()`.
     */
    virtual void set_string_output_modifier(
        value_reference reference,
        std::function<std::string(std::string_view)> modifier) = 0;
};

/**
 *  An interface for manipulators.
 *
 *  The methods in this interface represent various events that the manipulator
 *  may react to in some way. It may modify the slaves' variable values
 *  through the `simulator` interface at any time.
 */
class manipulator
{
public:
    /// A simulator was added to the execution.
    virtual void simulator_added(simulator_index, manipulable*, time_point) = 0;

    /// A simulator was removed from the execution.
    virtual void simulator_removed(simulator_index, time_point) = 0;

    /// A time step is commencing.
    virtual void step_commencing(
        time_point currentTime) = 0;

    virtual ~manipulator() noexcept {}
};

} // namespace cse

#endif
