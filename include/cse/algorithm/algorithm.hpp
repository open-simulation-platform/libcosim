#ifndef CSECORE_ALGORITHM_ALGORITHM_HPP
#define CSECORE_ALGORITHM_ALGORITHM_HPP

#include <cse/algorithm/simulator.hpp>
#include <cse/connection.hpp>
#include <cse/execution.hpp>
#include <cse/model.hpp>
#include <cse/observer/observer.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>

namespace cse
{

/**
 *  An interface for co-simulation algorithms.
 *
 *  A co-simulation algorithm is responsible for connecting variables (i.e.,
 *  transferring output values to the right input variables) and stepping
 *  simulators.
 *
 *  Some of the functions in this interface are guaranteed to be called in
 *  a fixed order:
 *
 *    1. `setup()`
 *    2. `initialize()`
 *    3. `do_step()` (possibly repeatedly)
 */
class algorithm
{
public:
    /**
     *  Adds a simulator to the co-simulation.
     *
     *  \param index
     *      A numerical index that will be used to identify the simulator
     *      in other function calls.
     *  \param sim
     *      A pointer to an object that is used to control the simulator.
     *      Note that the algorithm does not have resource ownership of
     *      the object it points to (i.e., should not try to delete it).
     */
    virtual void add_simulator(simulator_index index, simulator* sim) = 0;

    /**
     *  Removes a simulator from the co-simulation.
     *
     *  \param index
     *      The index of a simulator that has previously been added to the
     *      co-simulation with `add_simulator()`.
     */
    virtual void remove_simulator(simulator_index index) = 0;

    /**
     * Adds a connection to the co-simulation.
     *
     * After this, the algorithm is responsible for acquiring the values of
     * the connection's source variables, and distributing the connection's
     * destination variable values at communication points.
     *
     * It is assumed that the variables contained by the connection are valid
     * and that there are no existing connections to any of the connection's
     * destination variables.
     */
    virtual void add_connection(std::shared_ptr<connection> conn) = 0;

    /**
     * Removes a connection from the co-simulation.
     *
     * It is assumed that the connection has previously been added to the
     * co-simulation with `add_connection()`.
     */
    virtual void remove_connection(std::shared_ptr<connection> conn) = 0;

    /**
     *  Performs initial setup.
     *
     *  This function is guaranteed to be called before `initialize()`.
     *
     *  \param startTime
     *      The logical time at which the simulation will start. (The first
     *      time `do_step()` is called, its `currentT` argument will have
     *      the same value.)
     *  \param stopTime
     *      The logical time at which the simulation will end. If specified,
     *      the algorithm will not be stepped beyond this point.
     */
    virtual void setup(time_point startTime, std::optional<time_point> stopTime) = 0;

    /**
     *  Initializes the co-simulation.
     *
     *  Typically, this function will set the initial values of simulator
     *  variables, possibly performing iterative initialization to steady-state
     *  values for some of them.
     *
     *  This function is guaranteed to be called after `setup()` and before
     *  the first `do_step()` call.
     */
    virtual void initialize() = 0;

    /**
     *  Performs a single macro time step.
     *
     *  The actual time step length is determined by the algorithm, but it may
     *  not exceed `maxDeltaT` if specified.
     *
     *  This function is guaranteed to be called after `initialize()`. The
     *  first time it is called, `currentT` will be equal to the `startTime`
     *  argument passed to `setup()`.
     *
     *  \param currentT
     *      The starting point of the time step.
     *
     *  \returns
     *      The actual time step length, which must be less than or equal
     *      to `maxDeltaT`, if specified.
     */
    virtual std::pair<duration, std::unordered_set<simulator_index>> do_step(time_point currentT) = 0;

    virtual ~algorithm() noexcept = default;
};

} // namespace cse

#endif
