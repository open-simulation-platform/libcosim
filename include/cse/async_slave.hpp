/**
 *  \file
 *  \brief Asynchronous slave interface.
 */
#ifndef CSE_ASYNC_SLAVE_HPP
#define CSE_ASYNC_SLAVE_HPP

#include <cse/model.hpp>
#include <cse/slave.hpp>

#include <boost/fiber/future/future.hpp>
#include <gsl/span>

#include <memory>
#include <optional>
#include <string>


namespace cse
{


/// Symbolic constants that represent the state of a slave.
enum class slave_state
{
    /**
     *  The slave exists but has not been configured yet.
     *
     *  The slave is in this state from its creation until `setup()` is called.
     */
    created,

    /**
     *  The slave is in initialisation mode.
     *
     *  The slave is in this state from the time `setup()` is called and until
     *  `start_simulation()` is called.
     */
    initialisation,

    /**
     *  The slave is in simulation mode.
     *
     *  The slave is in this state from the time `start_simulation()` is called
     *  and until `end_simulation()` is called.
     */
    simulation,

    /**
     *  The slave is terminated.
     *
     *  The slave is in this state from the time `end_simulation()` is called
     *  and until its destruction.
     */
    terminated,

    /**
     *  An irrecoverable error occurred.
     *
     *  The slave is in this state from the time an exception is thrown and
     *  until its destruction.
     */
    error,

    /**
     *  The slave is in an indeterminate state.
     *
     *  This is the case when a state-changing asynchronous function call is
     *  currently in progress.
     */
    indeterminate,
};


/**
 *  An asynchronous co-simulation slave interface.
 *
 *  This is an asynchronous variant of `cse::slave`, typically used for
 *  remote control of slaves (e.g. through some form of RPC).
 *
 *  The interface is completely analogous to `cse::slave`, and the functions
 *  in the two classes are for the most part subject to the same constraints
 *  and requirements.  We therefore refer the reader to the `cse::slave`
 *  documentation for details.
 *
 *  The primary distinguishing feature of the asynchronous interface is that
 *  all functions are designed to be non-blocking.  Therefore, they return
 *  [`boost::fibers::future`](https://www.boost.org/doc/libs/release/libs/fiber/doc/html/fiber/synchronization/futures/future.html)
 *  objects that can be polled for the actual results when they are needed,
 *  thus allowing other operations to be carried out in the meantime.
 *
 *  Whenever one of these functions are called, it is required that the
 *  operation be allowed to complete before a new function call is made;
 *  the implementing class is not required to support multiple operations
 *  in parallel.
 *
 *  The use of [Boost.Fiber](https://www.boost.org/doc/libs/release/libs/fiber/doc/html/index.html)
 *  indicates that an implementing class must support cooperative multitasking
 *  using fibers.
 */
class async_slave
{
public:
    virtual ~async_slave() = default;

    /// Returns the slave's current state.
    virtual slave_state state() const noexcept = 0;

    /**
     *  Returns a model description.
     *
     *  \pre
     *      `state()` is *not* `slave_state::error` or
     *      `slave_state::indeterminate`.
     *  \post
     *      `state()` is `slave_state::indeterminate` until the asynchronous
     *      call completes, after which it returns to its previous  state
     *      or `slave_state::error`.
     *
     *  \see slave::model_description()
     */
    virtual boost::fibers::future<cse::model_description> model_description() = 0;

    /**
     *  Instructs the slave to perform pre-simulation setup and enter
     *  initialisation mode.
     *
     *  \pre
     *      `state()` is `slave_state::created`
     *  \post
     *      `state()` is `slave_state::indeterminate` until the asynchronous
     *      call completes, after which it is `slave_state::initialisation`
     *      or `slave_state::error`.
     *
     *  \see slave::setup()
     */
    virtual boost::fibers::future<void> setup(
        time_point startTime,
        std::optional<time_point> stopTime,
        std::optional<double> relativeTolerance) = 0;

    /**
     *  Informs the slave that the initialisation stage ends and the
     *  simulation begins.
     *
     *  \pre
     *      `state()` is `slave_state::initialisation`
     *  \post
     *      `state()` is `slave_state::indeterminate` until the asynchronous
     *      call completes, after which it is `slave_state::simulation`
     *      or `slave_state::error`.
     *
     *  \see slave::start_simulation()
     */
    virtual boost::fibers::future<void> start_simulation() = 0;

    /**
     *  Informs the slave that the simulation run has ended.
     *
     *  \pre
     *      `state()` is `slave_state::simulation`
     *  \post
     *      `state()` is `slave_state::indeterminate` until the asynchronous
     *      call completes, after which it is `slave_state::terminated`
     *      or `slave_state::error`.
     *
     *  \see slave::end_simulation()
     */
    virtual boost::fibers::future<void> end_simulation() = 0;

    /**
     *  Performs model calculations for the time step which starts at
     *  the time point `currentT` and has a duration of `deltaT`.
     *
     *  \pre
     *      `state()` is `slave_state::simulation`
     *  \post
     *      `state()` is `slave_state::indeterminate` until the asynchronous
     *      call completes, after which it is `slave_state::simulation`
     *      or `slave_state::error`.
     *
     *  \see slave::do_step()
     */
    virtual boost::fibers::future<step_result> do_step(
        time_point currentT,
        duration deltaT) = 0;

    /// Result type for `get_variables()`.
    struct variable_values
    {
        /// Real variable values.
        gsl::span<double> real;

        /// Integer variable values.
        gsl::span<int> integer;

        /// Boolean variable values.
        gsl::span<bool> boolean;

        /// String variable values.
        gsl::span<std::string> string;
    };

    /**
     *  Retrieves variable values.
     *
     *  This combines the roles of all the `get_<type>_variables()` functions
     *  in `cse::slave`, to allow for more efficient transfer of variable
     *  values in situations where this may be an issue (e.g. over networks).
     *
     *  The returned `variable_values::<type>` arrays will be filled with the
     *  values of the variables specified in the corresponding `<type>Variables`
     *  arrays, in the same order.
     *
     *  The `variable_values` arrays are owned by the `async_slave` object, and
     *  are guaranteed to remain valid until another function is called on the
     *  same object.
     *
     *  \pre
     *      `state()` is `slave_state::initialisation` or
     *      `slave_state::simulation`
     *  \post
     *      `state()` is `slave_state::indeterminate` until the asynchronous
     *      call completes, after which it returns to its previous state
     *      or `slave_state::error`.
     *
     *  \see slave::get_real_variables()
     *  \see slave::get_integer_variables()
     *  \see slave::get_boolean_variables()
     *  \see slave::get_string_variables()
     */
    virtual boost::fibers::future<variable_values> get_variables(
        gsl::span<const value_reference> realVariables,
        gsl::span<const value_reference> integerVariables,
        gsl::span<const value_reference> booleanVariables,
        gsl::span<const value_reference> stringVariables) = 0;

    /**
     *  Sets variable values.
     *
     *  This combines the roles of all the `set_<type>_variables()` functions
     *  in `cse::slave`, to allow for more efficient transfer of variable
     *  values in situations where this may be an issue (e.g. over networks).
     *
     *  The function will set the value of each variable specified in each
     *  `<type>Variables` array to the value given in the corresponding
     *  element of the corresponding `<type>Values` array.
     *
     *  \pre
     *      `<type>Variables.size() == <type>Values.size()`
     *  \pre
     *      `state()` is `slave_state::initialisation` or
     *      `slave_state::simulation`
     *  \post
     *      `state()` is `slave_state::indeterminate` until the asynchronous
     *      call completes, after which it returns to its previous state
     *      or `slave_state::error`.
     *
     *  \see slave::set_real_variables()
     *  \see slave::set_integer_variables()
     *  \see slave::set_boolean_variables()
     *  \see slave::set_string_variables()
     */
    virtual boost::fibers::future<void> set_variables(
        gsl::span<const value_reference> realVariables,
        gsl::span<const double> realValues,
        gsl::span<const value_reference> integerVariables,
        gsl::span<const int> integerValues,
        gsl::span<const value_reference> booleanVariables,
        gsl::span<const bool> booleanValues,
        gsl::span<const value_reference> stringVariables,
        gsl::span<const std::string> stringValues) = 0;
};


/**
 *  Wraps a synchronous slave in an asynchronous interface.
 *
 *  The resulting `async_slave` is not actually asynchronous, as all function
 *  calls will be executed in fibers in the current thread (hence the "pseudo"
 *  modifier).
 */
std::shared_ptr<async_slave> make_pseudo_async(std::shared_ptr<slave> s);


/**
 *  Runs a slave in another thread.
 *
 *  This function will create a new thread for running `slave`.  Any
 *  (asynchronous) function call on the returned `async_slave` will be
 *  communicated to this "background thread" and executed (synchronously)
 *  there.
 *
 *  The background thread will be terminated if and only if the slave
 *  transitions to the states `slave_state::terminated` or
 *  `slave_state::error`.
 */
std::shared_ptr<async_slave> make_background_thread_slave(std::shared_ptr<slave> slave);


} // namespace cse
#endif // header guard
