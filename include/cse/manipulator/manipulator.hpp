/**
 *  \file
 *  \brief Defines the `manipulator` interface.
 */

#ifndef CSECORE_MANIPULATOR_MANIPULATOR_HPP
#define CSECORE_MANIPULATOR_MANIPULATOR_HPP

#include <cse/algorithm.hpp>
#include <cse/model.hpp>

namespace cse
{

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
    virtual void simulator_added(simulator_index, simulator*, time_point) = 0;

    /// A simulator was removed from the execution.
    virtual void simulator_removed(simulator_index, time_point) = 0;

    /// A time step is commencing.
    virtual void step_commencing(
        time_point currentTime) = 0;

    virtual ~manipulator() noexcept {}
};

} // namespace cse

#endif
