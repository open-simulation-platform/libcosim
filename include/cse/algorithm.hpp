/**
 *  \file
 *  \brief Co-simulation algorithms.
 */
#ifndef CSE_ALGORITHM_HPP
#define CSE_ALGORITHM_HPP

#include <memory>
#include <optional>
#include <string_view>

#include <boost/fiber/future.hpp>

#include <cse/execution.hpp>
#include <cse/model.hpp>
#include <cse/observer/observer.hpp>


namespace cse
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
class simulator : public observable
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
    virtual void expose_for_setting(variable_type type, variable_index index) = 0;

    /**
     *  Sets the value of a real variable.
     *
     *  The variable must previously have been exposed with `expose_for_setting()`.
     */
    virtual void set_real(variable_index index, double value) = 0;

    /**
     *  Sets the value of an integer variable.
     *
     *  The variable must previously have been exposed with `expose_for_setting()`.
     */
    virtual void set_integer(variable_index index, int value) = 0;

    /**
     *  Sets the value of a boolean variable.
     *
     *  The variable must previously have been exposed with `expose_for_setting()`.
     */
    virtual void set_boolean(variable_index index, bool value) = 0;

    /**
     *  Sets the value of a string variable.
     *
     *  The variable must previously have been exposed with `expose_for_setting()`.
     */
    virtual void set_string(variable_index index, std::string_view value) = 0;

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



    /**
     *  Updates the simulator with new input values and makes it calculate
     *  new output values, without advancing logical time.
     *
     *  This function can be used in the initialisation phase, after `setup()`
     *  has been called and before the first `do_step()` call.  It enables
     *  iterative initialisation of the system.  The purpose could be to
     *  propagate initial values between simulators and/or bring the system
     *  to a steady state.
     */
    virtual boost::fibers::future<void> do_iteration() = 0;

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
     *  Connects an output variable to an input variable.
     *
     *  After this, the algorithm is responsible for acquiring the value of
     *  the output variable and assigning it to the input variable at
     *  communication points.
     *
     *  \param output
     *      A reference to the output variable.
     *  \param input
     *      A reference to the input variable.
     *  \param inputAlreadyConnected
     *      Whether the input has already been connected in a previous
     *      `connect_variables()` call. If so, the previous connection must
     *      be broken. This is meant as an aid to subclass implementors,
     *      saving them from having to perform this check on every connection.
     */
    virtual void connect_variables(
        variable_id output,
        variable_id input,
        bool inputAlreadyConnected) = 0;

    /// Breaks a previously established connection to input variable `input`.
    virtual void disconnect_variable(variable_id input) = 0;

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
     *  \param maxDeltaT
     *      The maximum length of the time step (optional).
     *
     *  \returns
     *      The actual time step length, which must be less than or equal
     *      to `maxDeltaT`, if specified.
     */
    virtual duration do_step(time_point currentT, std::optional<duration> maxDeltaT) = 0;

    virtual ~algorithm() noexcept = default;
};


/**
 *  A fixed-stepsize co-simulation algorithm.
 *
 *  This is the simplest possible implementation of `algorithm`. All
 *  simulators are advanced in parallel with the same, constant
 *  communication interval at each step.
 */
class fixed_step_algorithm : public algorithm
{
public:
    /**
     *  Constructor.
     *
     *  \param stepSize
     *      The communication interval length.
     */
    explicit fixed_step_algorithm(duration stepSize);

    ~fixed_step_algorithm() noexcept;

    fixed_step_algorithm(const fixed_step_algorithm&) = delete;
    fixed_step_algorithm& operator=(const fixed_step_algorithm&) = delete;

    fixed_step_algorithm(fixed_step_algorithm&&) noexcept;
    fixed_step_algorithm& operator=(fixed_step_algorithm&&) noexcept;

    // `algorithm` methods
    void add_simulator(simulator_index i, simulator* s) override;
    void remove_simulator(simulator_index i) override;
    void connect_variables(
        variable_id output,
        variable_id input,
        bool inputAlreadyConnected) override;
    void disconnect_variable(variable_id input) override;
    void setup(time_point startTime, std::optional<time_point> stopTime) override;
    void initialize() override;
    duration do_step(time_point currentT, std::optional<duration> maxDeltaT) override;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};


} // namespace cse
#endif // header guard
