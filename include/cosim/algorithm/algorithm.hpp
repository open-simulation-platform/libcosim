/**
 *  \file
 *  Defines the cosim::algorithm interface
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef LIBCOSIM_ALGORITHM_ALGORITHM_HPP
#define LIBCOSIM_ALGORITHM_ALGORITHM_HPP

#include <cosim/algorithm/simulator.hpp>
#include <cosim/execution.hpp>
#include <cosim/function/function.hpp>
#include <cosim/model_description.hpp>
#include <cosim/observer/observer.hpp>
#include <cosim/serialization.hpp>
#include <cosim/time.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <unordered_set>
#include <utility>

namespace cosim
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
     *  \param stepSizeHint
     *      The recommended co-simulation step size for this simulator.
     *      The algorithm is free to choose whether and how this is taken
     *      into account.  The algorithm is also responsible for selecting
     *      a sensible default if this is zero.
     */
    virtual void add_simulator(
        simulator_index index,
        simulator* sim,
        duration stepSizeHint) = 0;

    /**
     *  Removes a simulator from the co-simulation.
     *
     *  \param index
     *      The index of a simulator that has previously been added to the
     *      co-simulation with `add_simulator()`.
     */
    virtual void remove_simulator(simulator_index index) = 0;

    /**
     *  Adds a function to the co-simulation.
     *
     *  \param index
     *      A numerical index that will be used to identify the function
     *      in other function calls.
     *  \param fun
     *      A pointer to an object that is used to access the function.
     *      Note that the algorithm does not have resource ownership of
     *      the object it points to (i.e., should not try to delete it).
     */
    virtual void add_function(function_index index, function* fun) = 0;

    /**
     *  Connects a simulator output variable to a simulator input variable.
     *
     *  After this, the algorithm is responsible for acquiring the value of
     *  the output variable and assigning it to the input variable at
     *  communication points.
     *
     *  \param output
     *      A reference to the output variable.
     *  \param input
     *      A reference to the input variable.
     */
    virtual void connect_variables(variable_id output, variable_id input) = 0;

    /**
     *  Connects a simulator output variable to a function input variable.
     *
     *  After this, the algorithm is responsible for acquiring the value of
     *  the output variable and assigning it to the input variable before
     *  the function is calculated.
     *
     *  \param output
     *      A reference to the output variable.
     *  \param input
     *      A reference to the input variable.
     */
    virtual void connect_variables(variable_id output, function_io_id input) = 0;

    /**
     *  Connects a function output variable to a simulator input variable.
     *
     *  After this, the algorithm is responsible for acquiring the value of
     *  the output variable and assigning it to the input variable after
     *  the function is calculated.
     *
     *  \param output
     *      A reference to the output variable.
     *  \param input
     *      A reference to the input variable.
     */
    virtual void connect_variables(function_io_id output, variable_id input) = 0;

    /// Breaks any previously established connection to input variable `input`.
    virtual void disconnect_variable(variable_id input) = 0;

    /// Breaks any previously established connection to input variable `input`.
    virtual void disconnect_variable(function_io_id input) = 0;

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
     *  the first `do_step()` call. Furthermore, no more subsimulators and
     *  functions will be added or removed after `initialize()` has been called;
     *  that is, `{add,remove}_{simulator,function}()` will not be called again.
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

    /**
     *  Exports the current state of the algorithm.
     *
     *  Note that system-structural information should not be included in the
     *  data exported by this function, only internal, algorithm-specific data.
     *  This is because it will be assumed that the system structure is
     *  unchanged or has already been restored when the state is imported
     *  again, as explained in the `import_state()` function documentation.
     */
    virtual serialization::node export_current_state() const = 0;

    /**
     *  Imports a previously-exported algorithm state.
     *
     *  When this function is called, it should be assumed that the system
     *  structure is the same as when the state was exported. That is, either
     *
     *  1. this is the algorithm instance from which the state was exported,
     *     and the system structure actually hasn't changed
     *  2. this is a new instance, but the original system structure has been
     *     restored prior to calling this function
     *
     *  By "system structure", we here mean the subsimulator indexes, the
     *  function indexes, and the variable connections.
     *
     *  It is guaranteed that this function is never called before
     *  `initialize()`.
     */
    virtual void import_state(const serialization::node& exportedState) = 0;

    virtual ~algorithm() noexcept = default;
};

} // namespace cosim

#endif
