#include <cse/async_slave.hpp>

#include <exception>
#include <sstream>
#include <type_traits>
#include <utility>

#include <boost/container/vector.hpp>
#include <boost/fiber/fixedsize_stack.hpp>
#include <boost/fiber/future/async.hpp>

#include "cse/error.hpp"
#include "cse/exception.hpp"


namespace cse
{
namespace
{

/**
 *  Helper class which checks, sets and resets the state variable for
 *  an `async_slave`.
 *
 *  The constructors of this class take a reference to the `slave_state`
 *  variable to be managed, and immediately set it to `indeterminate`.
 *  On destruction, the managed variable will be automatically set to
 *  a specified value, or, if an exception is currently "in flight", to
 *  the special `error` value.
 */
class state_guard
{
public:
    /**
     *  Constructs a `state_guard` that sets `stateVariable` to `finalState`
     *  on destruction.
     */
    state_guard(slave_state& stateVariable, slave_state finalState)
        : stateVariable_(&stateVariable)
        , finalState_(finalState)
    {
        stateVariable = slave_state::indeterminate;
    }

    /**
     *  Constructs a `state_guard` that resets `stateVariable` to its original
     *  value on destruction.
     */
    state_guard(slave_state& stateVariable)
        : state_guard(stateVariable, stateVariable)
    {}

    /**
     *  Manually sets the managed variable to its final value and relinquishes
     *  control of it.  Does not check for exceptions.
     */
    void reset() noexcept
    {
        if (stateVariable_ != nullptr) {
            *stateVariable_ = finalState_;
            stateVariable_ = nullptr;
        }
    }

    ~state_guard() noexcept
    {
        if (stateVariable_ != nullptr) {
            if (std::uncaught_exceptions()) {
                *stateVariable_ = slave_state::error;
            } else {
                *stateVariable_ = finalState_;
            }
        }
    }

    // Disallow copying, so that we don't inadvertently end up in a situation
    // where multiple `state_guard` objects try to manage the same state.
    state_guard(const state_guard&) = delete;
    state_guard& operator=(const state_guard&) = delete;

    state_guard(state_guard&& other) noexcept
        : stateVariable_(other.stateVariable_)
        , finalState_(other.finalState_)
    {
        other.stateVariable_ = nullptr;
    }
    state_guard& operator=(state_guard&& other) noexcept
    {
        stateVariable_ = other.stateVariable_;
        finalState_ = other.finalState_;
        other.stateVariable_ = nullptr;
        return *this;
    }

private:
    slave_state* stateVariable_;
    slave_state finalState_;
};


/**
 *  Runs `f` in a new fiber with a bigger-than-default stack size.
 *
 *  The default fiber stack size in Boost.Fiber is just a few kilobytes.
 *  This function sets it to a more "normal" stack size of 1 MiB, because
 *  third-party model code (FMUs) should not have to care about the fact
 *  that it's running in a fiber.
 */
template<typename F>
auto big_stack_async(F&& f)
{
    constexpr std::size_t stackSize = 1024 * 1024;
    return boost::fibers::async(
        boost::fibers::launch::post,
        std::allocator_arg,
        boost::fibers::fixedsize_stack(stackSize),
        std::forward<F>(f));
}


// Copies the contents of `span` into a new vector.
template<typename T>
boost::container::vector<std::remove_cv_t<T>> to_vector(gsl::span<T> span)
{
    return {span.begin(), span.end()};
}


class pseudo_async_slave : public cse::async_slave
{
public:
    pseudo_async_slave(std::shared_ptr<cse::slave> slave)
        : slave_(std::move(slave))
        , state_(slave_state::created)
    {}

    ~pseudo_async_slave() = default;

    // Disable copy and move, since we leak `this` references.
    pseudo_async_slave(pseudo_async_slave&&) = delete;
    pseudo_async_slave& operator=(pseudo_async_slave&&) = delete;
    pseudo_async_slave(const pseudo_async_slave&) = delete;
    pseudo_async_slave& operator=(const pseudo_async_slave&) = delete;

    // cse::async_slave function implementations
    slave_state state() const noexcept override
    {
        return state_;
    }

    boost::fibers::future<cse::model_description> model_description() override
    {
        CSE_PRECONDITION(
            state_ != slave_state::error &&
            state_ != slave_state::indeterminate);
        return big_stack_async([=, _ = state_guard(state_)] {
            return slave_->model_description();
        });
    }

    boost::fibers::future<void> setup(
        cse::time_point startTime,
        std::optional<time_point> stopTime,
        std::optional<double> relativeTolerance)
        override
    {
        CSE_PRECONDITION(state_ == slave_state::created);
        return big_stack_async([=, _ = state_guard(state_, slave_state::initialisation)] {
            slave_->setup(startTime, stopTime, relativeTolerance);
        });
    }

    boost::fibers::future<void> start_simulation() override
    {
        CSE_PRECONDITION(state_ == slave_state::initialisation);
        return big_stack_async([=, _ = state_guard(state_, slave_state::simulation)] {
            slave_->start_simulation();
        });
    }

    boost::fibers::future<void> end_simulation() override
    {
        CSE_PRECONDITION(state_ == slave_state::simulation);
        return big_stack_async([=, _ = state_guard(state_, slave_state::terminated)] {
            slave_->end_simulation();
        });
    }

    boost::fibers::future<cse::step_result> do_step(
        cse::time_point currentT,
        cse::duration deltaT)
        override
    {
        CSE_PRECONDITION(state_ == slave_state::simulation);
        return big_stack_async([=, _ = state_guard(state_)] {
            return slave_->do_step(currentT, deltaT);
        });
    }

    // clang-format off

    boost::fibers::future<variable_values> get_variables(
        gsl::span<const cse::variable_index> realVariables,
        gsl::span<const cse::variable_index> integerVariables,
        gsl::span<const cse::variable_index> booleanVariables,
        gsl::span<const cse::variable_index> stringVariables)
        override
    {
        CSE_PRECONDITION(
            state_ == slave_state::initialisation ||
            state_ == slave_state::simulation);

        realBuffer_.resize(realVariables.size());
        integerBuffer_.resize(integerVariables.size());
        booleanBuffer_.resize(booleanVariables.size());
        stringBuffer_.resize(stringVariables.size());

        return big_stack_async(
        [
            =,
            _ = state_guard(state_),
            rvi = to_vector(realVariables),
            ivi = to_vector(integerVariables),
            bvi = to_vector(booleanVariables),
            svi = to_vector(stringVariables)
        ] {
            auto rva = gsl::make_span(realBuffer_);
            auto iva = gsl::make_span(integerBuffer_);
            auto bva = gsl::make_span(booleanBuffer_);
            auto sva = gsl::make_span(stringBuffer_);
            slave_->get_real_variables(gsl::make_span(rvi), rva);
            slave_->get_integer_variables(gsl::make_span(ivi), iva);
            slave_->get_boolean_variables(gsl::make_span(bvi), bva);
            slave_->get_string_variables(gsl::make_span(svi), sva);
            return variable_values{rva, iva, bva, sva};
        });
    }

    boost::fibers::future<void> set_variables(
        gsl::span<const cse::variable_index> realVariables,
        gsl::span<const double> realValues,
        gsl::span<const cse::variable_index> integerVariables,
        gsl::span<const int> integerValues,
        gsl::span<const cse::variable_index> booleanVariables,
        gsl::span<const bool> booleanValues,
        gsl::span<const cse::variable_index> stringVariables,
        gsl::span<const std::string> stringValues)
        override
    {
        CSE_PRECONDITION(
            state_ == slave_state::initialisation ||
            state_ == slave_state::simulation);

        return big_stack_async(
        [
            =,
            stateGuard = state_guard(state_),
            rvi = to_vector(realVariables),
            rva = to_vector(realValues),
            ivi = to_vector(integerVariables),
            iva = to_vector(integerValues),
            bvi = to_vector(booleanVariables),
            bva = to_vector(booleanValues),
            svi = to_vector(stringVariables),
            sva = to_vector(stringValues)
        ]() mutable {
            // `nonfatal_bad_value` gets special treatment here. Since it's supposed
            // to be non-fatal, it should not prevent the setting of other variable
            // types, and it should not set the state to `error`.
            bool nonfatalBadValue = false;
            std::stringstream errMsg;
            try {
                slave_->set_real_variables(gsl::make_span(rvi), gsl::make_span(rva));
            } catch (const nonfatal_bad_value& e) {
                nonfatalBadValue = true;
                errMsg << e.what() << std::endl;
            }
            try {
                slave_->set_integer_variables(gsl::make_span(ivi), gsl::make_span(iva));
            } catch (const nonfatal_bad_value& e) {
                nonfatalBadValue = true;
                errMsg << e.what() << std::endl;
            }
            try {
                slave_->set_boolean_variables(gsl::make_span(bvi), gsl::make_span(bva));
            } catch (const nonfatal_bad_value& e) {
                nonfatalBadValue = true;
                errMsg << e.what() << std::endl;
            }
            try {
                slave_->set_string_variables(gsl::make_span(svi), gsl::make_span(sva));
            } catch (const nonfatal_bad_value& e) {
                nonfatalBadValue = true;
                errMsg << e.what() << std::endl;
            }
            if (nonfatalBadValue) {
                stateGuard.reset();
                throw nonfatal_bad_value(errMsg.str());
            }
        });
    }

    // clang-format on

private:
    std::shared_ptr<cse::slave> slave_;
    slave_state state_;

    // We need Boost's vector<bool> to avoid the issues with std::vector<bool>.
    // The others are just for consistency.
    boost::container::vector<double> realBuffer_;
    boost::container::vector<int> integerBuffer_;
    boost::container::vector<bool> booleanBuffer_;
    boost::container::vector<std::string> stringBuffer_;
};
} // namespace


std::shared_ptr<async_slave> make_pseudo_async(std::shared_ptr<slave> s)
{
    return std::make_shared<pseudo_async_slave>(std::move(s));
}


} // namespace cse
