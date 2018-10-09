/**
 *  \file
 *  \brief  Some slave classes for use in tests.
 */
#ifndef CSE_TEST_MOCK_SLAVE_HPP
#define CSE_TEST_MOCK_SLAVE_HPP

#include <cassert>
#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <boost/container/vector.hpp>
#include <boost/fiber/all.hpp>
#include <gsl/span>

#include <cse/async_slave.hpp>
#include <cse/event_loop.hpp>
#include <cse/slave.hpp>


/**
 *  A simple slave implementation for testing purposes.
 *
 *  The slave has one input and one output of each of the four data types.
 *  When `do_step()` is called, it performs a user-defined operation on
 *  each of the inputs and places the results in the outputs. The default
 *  is just the identity operator.
 */
class mock_slave : public cse::slave
{
public:
    explicit mock_slave(
        std::function<double(double)> realOp = nullptr,
        std::function<int(int)> intOp = nullptr,
        std::function<bool(bool)> boolOp = nullptr,
        std::function<std::string(std::string_view)> stringOp = nullptr)
        : realOp_(std::move(realOp))
        , intOp_(std::move(intOp))
        , boolOp_(std::move(boolOp))
        , stringOp_(std::move(stringOp))
        , realIn_(0.0)
        , realOut_(1.0)
        , intIn_(0)
        , intOut_(1)
        , boolIn_(true)
        , boolOut_(false)
        , stringIn_()
        , stringOut_()
    {
    }

    cse::model_description model_description() const override
    {
        cse::model_description md;
        md.name = "mock_slave";
        md.uuid = "09b7ee06-fc07-4ad0-86f1-cd183fbae519";
        return md;
    }

    void setup(
        std::string_view /*slaveName*/,
        std::string_view /*executionName*/,
        cse::time_point /*startTime*/,
        cse::time_point /*stopTime*/,
        bool /*adaptiveStepSize*/,
        double /*relativeTolerance*/) override
    {
    }

    void start_simulation() override
    {
    }

    void end_simulation() override
    {
    }

    cse::step_result do_step(cse::time_point /*currentT*/, cse::time_duration /*deltaT*/) override
    {
        realOut_ = realOp_ ? realOp_(realIn_) : realIn_;
        intOut_ = intOp_ ? intOp_(intIn_) : intIn_;
        boolOut_ = boolOp_ ? boolOp_(boolIn_) : boolIn_;
        stringOut_ = stringOp_ ? stringOp_(stringIn_) : stringIn_;
        return cse::step_result::complete;
    }

    void get_real_variables(
        gsl::span<const cse::variable_index> variables,
        gsl::span<double> values) const override
    {
        for (int i = 0; i < variables.size(); ++i) {
            if (variables[i] == 0) {
                values[i] = realOut_;
            } else {
                throw std::out_of_range("bad index");
            }
        }
    }

    void get_integer_variables(
        gsl::span<const cse::variable_index> variables,
        gsl::span<int> values) const override
    {
        for (int i = 0; i < variables.size(); ++i) {
            if (variables[i] == 0) {
                values[i] = intOut_;
            } else {
                throw std::out_of_range("bad index");
            }
        }
    }

    void get_boolean_variables(
        gsl::span<const cse::variable_index> variables,
        gsl::span<bool> values) const override
    {
        for (int i = 0; i < variables.size(); ++i) {
            if (variables[i] == 0) {
                values[i] = boolOut_;
            } else {
                throw std::out_of_range("bad index");
            }
        }
    }

    void get_string_variables(
        gsl::span<const cse::variable_index> variables,
        gsl::span<std::string> values) const override
    {
        for (int i = 0; i < variables.size(); ++i) {
            if (variables[i] == 0) {
                values[i] = stringOut_;
            } else {
                throw std::out_of_range("bad index");
            }
        }
    }

    void set_real_variables(
        gsl::span<const cse::variable_index> variables,
        gsl::span<const double> values) override
    {
        for (int i = 0; i < variables.size(); ++i) {
            if (variables[i] == 0) {
                realIn_ = values[i];
            } else {
                throw std::out_of_range("bad index");
            }
        }
    }

    void set_integer_variables(
        gsl::span<const cse::variable_index> variables,
        gsl::span<const int> values) override
    {
        for (int i = 0; i < variables.size(); ++i) {
            if (variables[i] == 0) {
                intIn_ = values[i];
            } else {
                throw std::out_of_range("bad index");
            }
        }
    }

    void set_boolean_variables(
        gsl::span<const cse::variable_index> variables,
        gsl::span<const bool> values) override
    {
        for (int i = 0; i < variables.size(); ++i) {
            if (variables[i] == 0) {
                boolIn_ = values[i];
            } else {
                throw std::out_of_range("bad index");
            }
        }
    }

    void set_string_variables(
        gsl::span<const cse::variable_index> variables,
        gsl::span<const std::string> values) override
    {
        for (int i = 0; i < variables.size(); ++i) {
            if (variables[i] == 0) {
                stringIn_ = values[i];
            } else {
                throw std::out_of_range("bad index");
            }
        }
    }

private:
    std::function<double(double)> realOp_;
    std::function<int(int)> intOp_;
    std::function<bool(bool)> boolOp_;
    std::function<std::string(std::string_view)> stringOp_;

    double realIn_, realOut_;
    int intIn_, intOut_;
    bool boolIn_, boolOut_;
    std::string stringIn_, stringOut_;
};


/// Convenience function for making a `unique_ptr`-managed `mock_slave`.
inline std::unique_ptr<cse::slave> make_mock_slave()
{
    return std::make_unique<mock_slave>();
}


/**
 *  Uses an event loop to make the current fiber wait for some period,
 *  yielding to other fibers in the meantime.
 */
void loop_wait(
    std::shared_ptr<cse::event_loop> eventLoop,
    std::chrono::microseconds period);


/// Copies the contents of `span` into a new vector.
template<typename T>
boost::container::vector<std::remove_cv_t<T>> to_vector(gsl::span<T> span)
{
    return {span.begin(), span.end()};
}


/**
 *  An `async_slave` implementation that fakes some source of asynchronicity
 *  (e.g. network communications) by using `loop_wait()`.
 */
class async_slave_wrapper : public cse::async_slave
{
public:
    async_slave_wrapper(
        std::shared_ptr<cse::event_loop> eventLoop,
        std::unique_ptr<cse::slave> slave)
        : eventLoop_(std::move(eventLoop))
        , slave_(std::move(slave))
    {}

    ~async_slave_wrapper() = default;

    // Disable copy and move, since we leak `this` references.
    async_slave_wrapper(async_slave_wrapper&&) = delete;
    async_slave_wrapper& operator=(async_slave_wrapper&&) = delete;
    async_slave_wrapper(const async_slave_wrapper&) = delete;
    async_slave_wrapper& operator=(const async_slave_wrapper&) = delete;

    // cse::async_slave function implementations
    boost::fibers::future<cse::model_description> model_description() override
    {
        return boost::fibers::async([=]() {
            loop_wait(eventLoop_, std::chrono::milliseconds(1));
            return slave_->model_description();
        });
    }

    boost::fibers::future<void> setup(
        std::string_view slaveName,
        std::string_view executionName,
        cse::time_point startTime,
        cse::time_point stopTime,
        bool adaptiveStepSize,
        double relativeTolerance)
        override
    {
        return boost::fibers::async([=, sn = std::string(slaveName), en = std::string(executionName)]() {
            loop_wait(eventLoop_, std::chrono::milliseconds(1));
            slave_->setup(sn, en, startTime, stopTime, adaptiveStepSize, relativeTolerance);
        });
    }

    boost::fibers::future<void> start_simulation() override
    {
        return boost::fibers::async([=]() {
            loop_wait(eventLoop_, std::chrono::milliseconds(1));
            slave_->start_simulation();
        });
    }

    boost::fibers::future<void> end_simulation() override
    {
        return boost::fibers::async([=]() {
            loop_wait(eventLoop_, std::chrono::milliseconds(1));
            slave_->end_simulation();
        });
    }

    boost::fibers::future<cse::step_result> do_step(
        cse::time_point currentT,
        cse::time_duration deltaT)
        override
    {
        return boost::fibers::async([=]() {
            loop_wait(eventLoop_, std::chrono::milliseconds(10));
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
        realBuffer_.resize(realVariables.size());
        integerBuffer_.resize(integerVariables.size());
        booleanBuffer_.resize(booleanVariables.size());
        stringBuffer_.resize(stringVariables.size());

        return boost::fibers::async(
            [
                =,
                rvi = to_vector(realVariables),
                ivi = to_vector(integerVariables),
                bvi = to_vector(booleanVariables),
                svi = to_vector(stringVariables)
            ]() {
                loop_wait(eventLoop_, std::chrono::milliseconds(1));
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
        return boost::fibers::async(
            [
                =,
                rvi = to_vector(realVariables),
                rva = to_vector(realValues),
                ivi = to_vector(integerVariables),
                iva = to_vector(integerValues),
                bvi = to_vector(booleanVariables),
                bva = to_vector(booleanValues),
                svi = to_vector(stringVariables),
                sva = to_vector(stringValues)
            ]() {
                loop_wait(eventLoop_, std::chrono::milliseconds(1));
                // NOTE: We don't handle nonfatal_bad_value correctly here.
                // All functions should get called, and the exceptions should get merged.
                slave_->set_real_variables(gsl::make_span(rvi), gsl::make_span(rva));
                slave_->set_integer_variables(gsl::make_span(ivi), gsl::make_span(iva));
                slave_->set_boolean_variables(gsl::make_span(bvi), gsl::make_span(bva));
                slave_->set_string_variables(gsl::make_span(svi), gsl::make_span(sva));
            });
    }

    // clang-format on

private:
    std::shared_ptr<cse::event_loop> eventLoop_;
    std::unique_ptr<cse::slave> slave_;

    // We need Boost's vector<bool> to avoid the issues with std::vector<bool>.
    // The others are just for consistency.
    boost::container::vector<double> realBuffer_;
    boost::container::vector<int> integerBuffer_;
    boost::container::vector<bool> booleanBuffer_;
    boost::container::vector<std::string> stringBuffer_;
};


/**
 *  Convenience function for making a `unique_ptr`-managed `async_slave_wrapper`.
 *
 *  `SlaveType` must be a concrete subclass of `cse::slave`. Constructor
 *  arguments are forwarded to the `SlaveType` constructor.
 */
template<typename SlaveType, typename... CtorArgs>
std::unique_ptr<cse::async_slave> make_async_slave(
    std::shared_ptr<cse::event_loop> eventLoop,
    CtorArgs&&... ctorArgs)
{
    return std::make_unique<async_slave_wrapper>(
        std::move(eventLoop),
        std::make_unique<SlaveType>(std::forward<CtorArgs>(ctorArgs)...));
}


// A helper class for loop_wait().
class loop_waiter
    : private cse::timer_event_handler
{
public:
    loop_waiter(std::shared_ptr<cse::event_loop> eventLoop)
        : eventLoop_(std::move(eventLoop))
    {}

    ~loop_waiter() noexcept { assert(event_ == nullptr); }

    // Disable copy and move, since we leak `this` references.
    loop_waiter(loop_waiter&&) = delete;
    loop_waiter& operator=(loop_waiter&&) = delete;
    loop_waiter(const loop_waiter&) = delete;
    loop_waiter& operator=(const loop_waiter&) = delete;

    void wait(std::chrono::microseconds duration)
    {
        assert(!event_);
        event_ = eventLoop_->add_timer();
        event_->enable(duration, false, *this);
        std::unique_lock<boost::fibers::mutex> lock(mutex_);
        condition_.wait(lock);
    }

private:
    void handle_timer_event(cse::timer_event*) override
    {
        event_ = nullptr;
        condition_.notify_one();
    }

    std::shared_ptr<cse::event_loop> eventLoop_;
    cse::timer_event* event_ = nullptr;
    boost::fibers::mutex mutex_;
    boost::fibers::condition_variable condition_;
};

// Documented further up.
inline void loop_wait(
    std::shared_ptr<cse::event_loop> eventLoop,
    std::chrono::microseconds period)
{
    loop_waiter waiter(std::move(eventLoop));
    waiter.wait(period);
}

#endif // header guard
