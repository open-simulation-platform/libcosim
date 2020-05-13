/**
 *  \file
 *  Defines the simulator interface
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef LIBCOSIM_ALGORITHM_SIMULATOR_HPP
#define LIBCOSIM_ALGORITHM_SIMULATOR_HPP

#include <cosim/manipulator/manipulator.hpp>
#include <cosim/model.hpp>

#include <boost/fiber/future.hpp>

#include <functional>
#include <optional>
#include <string_view>
#include <unordered_set>

namespace cosim
{

/**
 *  A simulator interface for co-simulation algorithms.
 *
 *  This is the simulator interface exposed to `algorithm` implementers,
 *  and is used to control one "sub-simulator" in a co-simulation.
 *
 *  Some of the functions in this class, specifically the ones that return a
 *  `boost::fibers::future` object, are asynchronous.  Only one asynchronous
 *  operation may be executed per `simulator` object at any given time,
 *  meaning that client code must *always* ensure that the previous operation
 *  has completed before starting a new one. This is typically done by calling
 *  `boost::fibers::future::get()` on the future returned from the previous
 *  function call.
 */
class simulator : public manipulable
{
public:
    /**
     *  Sets the value of a real variable.
     *
     *  The variable must previously have been exposed with `expose_for_setting()`.
     */
    virtual void set_real(value_reference reference, double value) = 0;

    /**
     *  Sets the value of an integer variable.
     *
     *  The variable must previously have been exposed with `expose_for_setting()`.
     */
    virtual void set_integer(value_reference reference, int value) = 0;

    /**
     *  Sets the value of a boolean variable.
     *
     *  The variable must previously have been exposed with `expose_for_setting()`.
     */
    virtual void set_boolean(value_reference reference, bool value) = 0;

    /**
     *  Sets the value of a string variable.
     *
     *  The variable must previously have been exposed with `expose_for_setting()`.
     */
    virtual void set_string(value_reference reference, std::string_view value) = 0;

    /**
     *  Performs pre-simulation setup and enters initialisation mode.
     *
     *  This function must be called exactly once, before initialisation and
     *  simulation can begin (i.e. before the first time either of
     *  `do_iteration()` or `do_step()` are called).
     *
     *  \param startTime
     *      The logical time at which the simulation will start. (The first
     *      time `do_step()` is called, its `currentT` argument must have
     *      the same value.)
     *  \param stopTime
     *      The logical time at which the simulation will end. If specified,
     *      the simulator may not be stepped beyond this point.
     *  \param relativeTolerance
     *      A suggested relative tolerance for the internal error estimation
     *      of the simulator's solver.  This should be specified if the
     *      co-simulation algorithm itself uses error estimation to determine
     *      the length of communication intervals.  If the simulator's
     *      solver doesn't use error estimation, it will just ignore this
     *      parameter.
     */
    virtual boost::fibers::future<void> setup(
        time_point startTime,
        std::optional<time_point> stopTime,
        std::optional<double> relativeTolerance) = 0;

    /// Returns all value references of real type that currently have an active modifier.
    virtual const std::unordered_set<value_reference>& get_modified_real_variables() const = 0;

    /// Returns all value references of integer type that currently have an active modifier.
    virtual const std::unordered_set<value_reference>& get_modified_integer_variables() const = 0;

    /// Returns all value references of boolean type that currently have an active modifier.
    virtual const std::unordered_set<value_reference>& get_modified_boolean_variables() const = 0;

    /// Returns all value references of string type that currently have an active modifier.
    virtual const std::unordered_set<value_reference>& get_modified_string_variables() const = 0;

    /**
     *  Updates the simulator with new input values and makes it calculate
     *  new output values, without advancing logical time.
     *
     *  This function can be used in the initialisation phase, after `setup()`
     *  has been called and before the call to `start_simulation()`. It enables
     *  iterative initialisation of the system. The purpose could be to
     *  propagate initial values between simulators and/or bring the system
     *  to a steady state.
     */
    virtual boost::fibers::future<void> do_iteration() = 0;

    /**
     *  Signals to the simulator that the initialization phase is complete
     *  and that stepping will begin.
     *
     *  This function must be called at the end of the initialisation phase,
     *  after any call to `do_iteration()` and before the first `do_step()` call.
     */
    virtual boost::fibers::future<void> start_simulation() = 0;

    /**
     *  Performs a single time step.
     *
     *  This causes the simulator to perform its computations for the logical
     *  time interval from `currentT` to `currentT+deltaT`.
     *
     *  \param currentT
     *      The starting point of the time step.
     *  \param deltaT
     *      The length of the time step. Must be positive.
     */
    virtual boost::fibers::future<step_result> do_step(
        time_point currentT,
        duration deltaT) = 0;
};

} // namespace cosim

#endif
