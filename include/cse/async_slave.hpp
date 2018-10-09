/**
 *  \file
 *  \brief Asynchronous slave interface.
 */
#ifndef CSE_ASYNC_SLAVE_HPP
#define CSE_ASYNC_SLAVE_HPP

#include <memory>
#include <string>
#include <string_view>

#include <boost/fiber/future/future.hpp>
#include <gsl/span>

#include <cse/model.hpp>
#include <cse/slave.hpp>


namespace cse
{


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

    /**
     *  Returns a model description.
     *
     *  \see slave::model_description()
     */
    virtual boost::fibers::future<cse::model_description> model_description() = 0;

    /**
     *  Instructs the slave to perform pre-simulation setup and enter
     *  initialisation mode.
     *
     *  \see slave::setup()
     */
    virtual boost::fibers::future<void> setup(
        std::string_view slaveName,
        std::string_view executionName,
        time_point startTime,
        time_point stopTime,
        bool adaptiveStepSize,
        double relativeTolerance) = 0;

    /**
     *  Informs the slave that the initialisation stage ends and the
     *  simulation begins.
     *
     *  \see slave::start_simulation()
     */
    virtual boost::fibers::future<void> start_simulation() = 0;

    /**
     *  Informs the slave that the simulation run has ended.
     *
     *  \see slave::end_simulation()
     */
    virtual boost::fibers::future<void> end_simulation() = 0;

    /**
     *  Performs model calculations for the time step which starts at
     *  the time point `currentT` and has a duration of `deltaT`.
     *
     *  \see slave::start_simulation()
     */
    virtual boost::fibers::future<step_result> do_step(
        time_point currentT,
        time_duration deltaT) = 0;

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
     *  \see slave::get_real_variables()
     *  \see slave::get_integer_variables()
     *  \see slave::get_boolean_variables()
     *  \see slave::get_string_variables()
     */
    virtual boost::fibers::future<variable_values> get_variables(
        gsl::span<const variable_index> realVariables,
        gsl::span<const variable_index> integerVariables,
        gsl::span<const variable_index> booleanVariables,
        gsl::span<const variable_index> stringVariables) = 0;

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
     *  \pre `<type>Variables.size() == <type>Values.size()`
     *
     *  \see slave::set_real_variables()
     *  \see slave::set_integer_variables()
     *  \see slave::set_boolean_variables()
     *  \see slave::set_string_variables()
     */
    virtual boost::fibers::future<void> set_variables(
        gsl::span<const variable_index> realVariables,
        gsl::span<const double> realValues,
        gsl::span<const variable_index> integerVariables,
        gsl::span<const int> integerValues,
        gsl::span<const variable_index> booleanVariables,
        gsl::span<const bool> booleanValues,
        gsl::span<const variable_index> stringVariables,
        gsl::span<const std::string> stringValues) = 0;
};


/**
 *  Wraps a synchronous slave in an asynchronous interface.
 *
 *  The resulting `async_slave` is not actually asynchronous, as all function
 *  calls will be executed in fibers in the current thread (hence the "pseudo"
 *  modifier).
 */
std::unique_ptr<async_slave> make_pseudo_async(std::unique_ptr<slave> s);


} // namespace cse
#endif // header guard
