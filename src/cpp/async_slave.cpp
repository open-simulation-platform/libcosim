#include "cse/async_slave.hpp"

#include <exception>
#include <optional>
#include <sstream>
#include <type_traits>
#include <utility>
#include <variant>

#include <boost/container/vector.hpp>
#include <boost/fiber/fixedsize_stack.hpp>
#include <boost/fiber/future/async.hpp>

#include "cse/error.hpp"
#include "cse/exception.hpp"
#include "cse/utility/concurrency.hpp"


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


/**
 *  Sets variables of all types by calling all `slave.set_xxx_variables()`
 *  functions.
 *
 *  If one or more of them throw `nonfatal_bad_value`, the exceptions are
 *  collected, their messages are merged and a new `nonfatal_bad_value` is
 *  thrown at the end.
 */
void set_all_variables(
    slave& slave,
    gsl::span<const cse::variable_index> realVariables,
    gsl::span<const double> realValues,
    gsl::span<const cse::variable_index> integerVariables,
    gsl::span<const int> integerValues,
    gsl::span<const cse::variable_index> booleanVariables,
    gsl::span<const bool> booleanValues,
    gsl::span<const cse::variable_index> stringVariables,
    gsl::span<const std::string> stringValues)
{
    bool nonfatalBadValue = false;
    std::stringstream errMsg;
    try {
        slave.set_real_variables(realVariables, realValues);
    } catch (const nonfatal_bad_value& e) {
        nonfatalBadValue = true;
        errMsg << e.what() << std::endl;
    }
    try {
        slave.set_integer_variables(integerVariables, integerValues);
    } catch (const nonfatal_bad_value& e) {
        nonfatalBadValue = true;
        errMsg << e.what() << std::endl;
    }
    try {
        slave.set_boolean_variables(booleanVariables, booleanValues);
    } catch (const nonfatal_bad_value& e) {
        nonfatalBadValue = true;
        errMsg << e.what() << std::endl;
    }
    try {
        slave.set_string_variables(stringVariables, stringValues);
    } catch (const nonfatal_bad_value& e) {
        nonfatalBadValue = true;
        errMsg << e.what() << std::endl;
    }
    if (nonfatalBadValue) {
        throw nonfatal_bad_value(errMsg.str());
    }
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
            try {
                set_all_variables(
                    *slave_,
                    gsl::make_span(rvi), gsl::make_span(rva),
                    gsl::make_span(ivi), gsl::make_span(iva),
                    gsl::make_span(bvi), gsl::make_span(bva),
                    gsl::make_span(svi), gsl::make_span(sva));
            } catch (const nonfatal_bad_value&) {
                stateGuard.reset();
                throw;
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


// =============================================================================
// background_thread_slave
// =============================================================================

namespace
{

// Classes that represent requests/commands from the front end to the back end.
struct model_description_request
{
};
struct setup_request
{
    time_point startTime;
    std::optional<time_point> stopTime;
    std::optional<double> relativeTolerance;
};
struct start_simulation_request
{
};
struct end_simulation_request
{
};
struct do_step_request
{
    time_point currentT;
    duration deltaT;
};
struct get_variables_request
{
    gsl::span<const cse::variable_index> realVariables;
    gsl::span<double> realValues;
    gsl::span<const cse::variable_index> integerVariables;
    gsl::span<int> integerValues;
    gsl::span<const cse::variable_index> booleanVariables;
    gsl::span<bool> booleanValues;
    gsl::span<const cse::variable_index> stringVariables;
    gsl::span<std::string> stringValues;
};
struct set_variables_request
{
    gsl::span<const cse::variable_index> realVariables;
    gsl::span<const double> realValues;
    gsl::span<const cse::variable_index> integerVariables;
    gsl::span<const int> integerValues;
    gsl::span<const cse::variable_index> booleanVariables;
    gsl::span<const bool> booleanValues;
    gsl::span<const cse::variable_index> stringVariables;
    gsl::span<const std::string> stringValues;
};

// Classes that represent replies/results from the back end to the front end.
struct void_reply
{
};

// Variant types that can hold all the request and reply types.
using request_type = std::variant<
    model_description_request,
    setup_request,
    start_simulation_request,
    end_simulation_request,
    do_step_request,
    get_variables_request,
    set_variables_request>;
using reply_type = std::variant<
    void_reply,
    model_description,
    step_result,
    std::exception_ptr>;

// Communication channels for use between front end and back end.
using request_channel = utility::shared_box<request_type>;
using reply_channel = utility::shared_box<reply_type>;

/*
 *  Reads a reply from the given reply channel.
 *
 *  If the reply is of type `T`, it is returned.
 *  If the reply is a `std::exception_ptr`, its exception is thrown.
 *  If the reply is of any other type (which is a bug), the program terminates.
 */
template<typename T>
T get_reply(reply_channel& replyChannel)
{
    auto reply = replyChannel.take();
    if (const auto v = std::get_if<T>(&reply)) {
        return std::move(*v);
    } else if (const auto e = std::get_if<std::exception_ptr>(&reply)) {
        std::rethrow_exception(*e);
    } else {
        CSE_PANIC_M("Unexpected reply type");
    }
}

/*
 *  A generic visitor that can be constructed on the fly from a set of lambdas.
 *
 *  Inspired by:
 *  https://arne-mertz.de/2018/05/overload-build-a-variant-visitor-on-the-fly/
 */
template<typename... Functors>
struct visitor : Functors...
{
    visitor(const Functors&... functors)
        : Functors(functors)...
    {
    }

    using Functors::operator()...;
};

// An exception which signals normal shutdown of the background thread.
struct shutdown_background_thread
{
};


// The back-end function (which runs in the background thread).
void background_thread_slave_backend(
    std::shared_ptr<slave> slave,
    std::shared_ptr<request_channel> requestChannel,
    std::shared_ptr<reply_channel> replyChannel)
{
    for (;;) {
        try {
            std::visit(
                visitor(
                    [=](model_description_request) {
                        replyChannel->put(slave->model_description());
                    },
                    [=](const setup_request& r) {
                        slave->setup(r.startTime, r.stopTime, r.relativeTolerance);
                        replyChannel->put(void_reply());
                    },
                    [=](start_simulation_request) {
                        slave->start_simulation();
                        replyChannel->put(void_reply());
                    },
                    [=](end_simulation_request) {
                        slave->end_simulation();
                        replyChannel->put(void_reply());
                        throw shutdown_background_thread();
                    },
                    [=](const do_step_request& r) {
                        replyChannel->put(slave->do_step(r.currentT, r.deltaT));
                    },
                    [=](const get_variables_request& r) {
                        slave->get_real_variables(r.realVariables, r.realValues);
                        slave->get_integer_variables(r.integerVariables, r.integerValues);
                        slave->get_boolean_variables(r.booleanVariables, r.booleanValues);
                        slave->get_string_variables(r.stringVariables, r.stringValues);
                        replyChannel->put(void_reply());
                    },
                    [=](const set_variables_request& r) {
                        set_all_variables(
                            *slave,
                            r.realVariables, r.realValues,
                            r.integerVariables, r.integerValues,
                            r.booleanVariables, r.booleanValues,
                            r.stringVariables, r.stringValues);
                        replyChannel->put(void_reply());
                    }),
                requestChannel->take());
        } catch (const nonfatal_bad_value&) {
            replyChannel->put(std::current_exception());
            // continue
        } catch (shutdown_background_thread) {
            return;
        } catch (...) {
            replyChannel->put(std::current_exception());
            return;
        }
    }
}


// The front-end class (whose functions get called in a "foreground" thread).
class background_thread_slave_frontend : public async_slave
{
public:
    background_thread_slave_frontend(
        std::shared_ptr<request_channel> requestChannel,
        std::shared_ptr<reply_channel> replyChannel)
        : requestChannel_(requestChannel)
        , replyChannel_(replyChannel)
        , state_(slave_state::created)
    {
    }

    ~background_thread_slave_frontend() = default;

    // Disable copy and move, since we leak `this` references.
    background_thread_slave_frontend(background_thread_slave_frontend&&) = delete;
    background_thread_slave_frontend& operator=(background_thread_slave_frontend&&) = delete;
    background_thread_slave_frontend(const background_thread_slave_frontend&) = delete;
    background_thread_slave_frontend& operator=(const background_thread_slave_frontend&) = delete;

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
        return boost::fibers::async([=, _ = state_guard(state_)] {
            requestChannel_->put(model_description_request());
            return get_reply<cse::model_description>(*replyChannel_);
        });
    }

    boost::fibers::future<void> setup(
        time_point startTime,
        std::optional<time_point> stopTime,
        std::optional<double> relativeTolerance) override
    {
        CSE_PRECONDITION(state_ == slave_state::created);
        return boost::fibers::async([=, _ = state_guard(state_, slave_state::initialisation)] {
            requestChannel_->put(setup_request{startTime, stopTime, relativeTolerance});
            get_reply<void_reply>(*replyChannel_);
        });
    }

    boost::fibers::future<void> start_simulation() override
    {
        CSE_PRECONDITION(state_ == slave_state::initialisation);
        return boost::fibers::async([=, _ = state_guard(state_, slave_state::simulation)] {
            requestChannel_->put(start_simulation_request{});
            get_reply<void_reply>(*replyChannel_);
        });
    }

    boost::fibers::future<void> end_simulation() override
    {
        CSE_PRECONDITION(state_ == slave_state::simulation);
        return boost::fibers::async([=, _ = state_guard(state_, slave_state::terminated)] {
            requestChannel_->put(end_simulation_request{});
            get_reply<void_reply>(*replyChannel_);
        });
    }

    boost::fibers::future<step_result> do_step(
        time_point currentT,
        duration deltaT) override
    {
        CSE_PRECONDITION(state_ == slave_state::simulation);
        return boost::fibers::async([=, _ = state_guard(state_)] {
            requestChannel_->put(do_step_request{currentT, deltaT});
            return get_reply<step_result>(*replyChannel_);
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

        return boost::fibers::async(
        [
            =,
            _ = state_guard(state_),
            rvi = to_vector(realVariables),
            ivi = to_vector(integerVariables),
            bvi = to_vector(booleanVariables),
            svi = to_vector(stringVariables)
        ] {
            variable_values vv{
                gsl::make_span(realBuffer_),
                gsl::make_span(integerBuffer_),
                gsl::make_span(booleanBuffer_),
                gsl::make_span(stringBuffer_)};
            requestChannel_->put(
                get_variables_request{
                    gsl::make_span(rvi),
                    vv.real,
                    gsl::make_span(ivi),
                    vv.integer,
                    gsl::make_span(bvi),
                    vv.boolean,
                    gsl::make_span(svi),
                    vv.string});
            get_reply<void_reply>(*replyChannel_);
            return vv;
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

        return boost::fibers::async(
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
            requestChannel_->put(
                set_variables_request{
                    gsl::make_span(rvi),
                    gsl::make_span(rva),
                    gsl::make_span(ivi),
                    gsl::make_span(iva),
                    gsl::make_span(bvi),
                    gsl::make_span(bva),
                    gsl::make_span(svi),
                    gsl::make_span(sva)});
            try {
                get_reply<void_reply>(*replyChannel_);
            } catch (const nonfatal_bad_value&) {
                stateGuard.reset();
                throw;
            }
        });
    }

private:
    std::shared_ptr<request_channel> requestChannel_;
    std::shared_ptr<reply_channel> replyChannel_;
    slave_state state_;

    boost::container::vector<double> realBuffer_;
    boost::container::vector<int> integerBuffer_;
    boost::container::vector<bool> booleanBuffer_;
    boost::container::vector<std::string> stringBuffer_;
};

} // namespace


std::shared_ptr<async_slave> make_background_thread_slave(std::shared_ptr<slave> slave)
{
    CSE_INPUT_CHECK(slave);
    auto req = std::make_shared<request_channel>();
    auto rep = std::make_shared<reply_channel>();
    std::thread(background_thread_slave_backend, slave, req, rep).detach();
    return std::make_shared<background_thread_slave_frontend>(req, rep);
}


} // namespace cse
