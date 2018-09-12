#include <cassert>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

// We use Boost's vector in some places to work around the issues with
// std::vector<bool>
#include <boost/container/vector.hpp>
#include <boost/fiber/all.hpp>

#include <cse/async_slave.hpp>
#include <cse/event_loop.hpp>
#include <cse/libevent.hpp>
#include <cse/slave.hpp>

using namespace std::chrono_literals;


// Creates a mock cse::slave instance (defined further down)
std::unique_ptr<cse::slave> make_mock_slave();

// Creates a mock cse::async_slave instance (defined further down)
std::unique_ptr<cse::async_slave> make_mock_async_slave(
    cse::event_loop&,
    cse::slave&);


// This class is to make the event loop pass control to Boost's fiber scheduler
// every now and then.
class yield_handler : public cse::timer_event_handler
{
    void handle_timer_event(cse::timer_event*) override
    {
        boost::this_fiber::yield();
    }
};


// A helper macro to test various assertions
#define REQUIRE(test) \
    if (!(test)) throw std::runtime_error("Requirement not satisfied: " #test)


int main()
{
    using boost::fibers::fiber;
    using boost::fibers::future;

    // Create an event loop and make it fiber friendly.
    auto eventLoop = cse::make_libevent_event_loop();
    yield_handler yieldHandler;
    eventLoop->add_timer()->enable(1ms, true, yieldHandler);

    // Spin off the event loop in a separate fiber.
    auto eventLoopFiber = fiber([&]() { eventLoop->loop(); });

    try {
        constexpr int numSlaves = 10;
        constexpr double startTime = 0.0;
        constexpr double endTime = 1.0;
        constexpr double stepSize = 0.1;

        // Create the slaves
        std::vector<std::unique_ptr<cse::slave>> slaves;
        std::vector<std::unique_ptr<cse::async_slave>> asyncSlaves;
        for (int i = 0; i < numSlaves; ++i) {
            slaves.push_back(make_mock_slave());
            asyncSlaves.push_back(make_mock_async_slave(*eventLoop, *slaves.back()));
        }

        // Get model descriptions from all slaves
        std::vector<future<cse::model_description>> modelDescriptions;
        for (const auto& slave : asyncSlaves) {
            modelDescriptions.push_back(slave->model_description());
        }
        for (auto& md : modelDescriptions) {
            REQUIRE(md.get().name == "mock_slave");
        }

        // Setup all slaves
        std::vector<future<void>> setupResults;
        for (const auto& slave : asyncSlaves) {
            setupResults.push_back(
                slave->setup("", "", startTime, endTime, false, 0.0));
        }
        for (auto& r : setupResults) {
            r.get(); // Throws if an operation failed
        }

        // Start simulation
        std::vector<future<void>> startResults;
        for (const auto& slave : asyncSlaves) {
            startResults.push_back(slave->start_simulation());
        }
        for (auto& r : startResults) {
            r.get();
        }

        // Simulation
        for (double t = startTime; t <= endTime; t += stepSize) {
            // Perform time steps
            std::vector<future<bool>> stepResults;
            for (const auto& slave : asyncSlaves) {
                stepResults.push_back(slave->do_step(t, stepSize));
            }
            for (auto& r : stepResults) {
                REQUIRE(r.get());
            }

            // Get variable values. For now, we simply get the value of each
            // slave's sole real output variable.
            std::vector<future<cse::async_slave::variable_values>> getResults;
            for (const auto& slave : asyncSlaves) {
                const cse::variable_index realOutIndex = 0;
                getResults.push_back(
                    slave->get_variables({&realOutIndex, 1}, {}, {}, {}));
            }
            std::vector<double> values; // To be filled with one value per slave
            for (auto& r : getResults) {
                values.push_back(r.get().real[0]);
            }

            // Set variable values. We connect the slaves such that slave N's
            // input is assigned slave N+1's output, simply by rotating the
            // value vector elements.
            std::rotate(values.begin(), values.begin() + 1, values.end());

            std::vector<future<void>> setResults;
            for (int i = 0; i < numSlaves; ++i) {
                const cse::variable_index realOutIndex = 0;
                setResults.push_back(
                    asyncSlaves[i]->set_variables(
                        {&realOutIndex, 1}, {&values[i], 1},
                        {}, {},
                        {}, {},
                        {}, {}));
            }
            for (auto& r : setResults) {
                r.get();
            }
        }

        // End simulation
        std::vector<future<void>> endResults;
        for (const auto& slave : asyncSlaves) {
            endResults.push_back(slave->end_simulation());
        }
        for (auto& r : endResults) {
            r.get();
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    eventLoop->stop_soon();
    eventLoopFiber.join();
    return 0;
}


// A trivial slave implementation
class mock_slave : public cse::slave
{
public:
    mock_slave()
        : realIn_(0.0)
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

    bool do_step(cse::time_point /*currentT*/, cse::time_duration /*deltaT*/) override
    {
        realOut_ = realIn_ + 1.0;
        intOut_ = intIn_ + 1;
        boolOut_ = !boolIn_;
        stringOut_ = stringIn_ + stringIn_;
        return true;
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
    double realIn_, realOut_;
    int intIn_, intOut_;
    bool boolIn_, boolOut_;
    std::string stringIn_, stringOut_;
};

// Documented further up.
std::unique_ptr<cse::slave> make_mock_slave()
{
    return std::make_unique<mock_slave>();
}


// Uses the event loop to make the current fiber wait for some period,
// yielding to other fibers in the meantime.
void loop_wait(cse::event_loop& eventLoop, std::chrono::microseconds period);


// Copies the contents of `span` into a new vector.
template<typename T>
boost::container::vector<std::remove_cv_t<T>> to_vector(gsl::span<T> span)
{
    return {span.begin(), span.end()};
}


// An `async_slave` implementation that fakes some source of asynchronicity
// (e.g. network communications) by using `loop_wait()`.
class mock_async_slave : public cse::async_slave
{
public:
    mock_async_slave(cse::event_loop& eventLoop, cse::slave& slave)
        : eventLoop_(eventLoop)
        , slave_(slave)
    {}

    ~mock_async_slave() = default;

    // Disable copy and move, since we leak `this` references.
    mock_async_slave(mock_async_slave&&) = delete;
    mock_async_slave& operator=(mock_async_slave&&) = delete;
    mock_async_slave(const mock_async_slave&) = delete;
    mock_async_slave& operator=(const mock_async_slave&) = delete;

    // cse::async_slave function implementations
    boost::fibers::future<cse::model_description> model_description() override
    {
        return boost::fibers::async([=]() {
            loop_wait(eventLoop_, 1ms);
            return slave_.model_description();
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
            loop_wait(eventLoop_, 1ms);
            slave_.setup(sn, en, startTime, stopTime, adaptiveStepSize, relativeTolerance);
        });
    }

    boost::fibers::future<void> start_simulation() override
    {
        return boost::fibers::async([=]() {
            loop_wait(eventLoop_, 1ms);
            slave_.start_simulation();
        });
    }

    boost::fibers::future<void> end_simulation() override
    {
        return boost::fibers::async([=]() {
            loop_wait(eventLoop_, 1ms);
            slave_.end_simulation();
        });
    }

    boost::fibers::future<bool> do_step(
        cse::time_point currentT,
        cse::time_duration deltaT)
        override
    {
        return boost::fibers::async([=]() {
            loop_wait(eventLoop_, 10ms);
            return slave_.do_step(currentT, deltaT);
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
                loop_wait(eventLoop_, 1ms);
                auto rva = gsl::make_span(realBuffer_);
                auto iva = gsl::make_span(integerBuffer_);
                auto bva = gsl::make_span(booleanBuffer_);
                auto sva = gsl::make_span(stringBuffer_);
                slave_.get_real_variables(gsl::make_span(rvi), rva);
                slave_.get_integer_variables(gsl::make_span(ivi), iva);
                slave_.get_boolean_variables(gsl::make_span(bvi), bva);
                slave_.get_string_variables(gsl::make_span(svi), sva);
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
                loop_wait(eventLoop_, 1ms);
                // NOTE: We don't handle nonfatal_bad_value correctly here.
                // All functions should get called, and the exceptions should get merged.
                slave_.set_real_variables(gsl::make_span(rvi), gsl::make_span(rva));
                slave_.set_integer_variables(gsl::make_span(ivi), gsl::make_span(iva));
                slave_.set_boolean_variables(gsl::make_span(bvi), gsl::make_span(bva));
                slave_.set_string_variables(gsl::make_span(svi), gsl::make_span(sva));
            });
    }

    // clang-format on

private:
    cse::event_loop& eventLoop_;
    cse::slave& slave_;

    // We need Boost's vector<bool> to avoid the issues with std::vector<bool>.
    // The others are just for consistency.
    boost::container::vector<double> realBuffer_;
    boost::container::vector<int> integerBuffer_;
    boost::container::vector<bool> booleanBuffer_;
    boost::container::vector<std::string> stringBuffer_;
};

// Documented further up.
std::unique_ptr<cse::async_slave> make_mock_async_slave(
    cse::event_loop& eventLoop,
    cse::slave& slave)
{
    return std::make_unique<mock_async_slave>(eventLoop, slave);
}


// A helper class for loop_wait().
class loop_waiter
    : private cse::timer_event_handler
{
public:
    loop_waiter(cse::event_loop& eventLoop)
        : eventLoop_(eventLoop)
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
        event_ = eventLoop_.add_timer();
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

    cse::event_loop& eventLoop_;
    cse::timer_event* event_ = nullptr;
    boost::fibers::mutex mutex_;
    boost::fibers::condition_variable condition_;
};

// Documented further up.
void loop_wait(cse::event_loop& eventLoop, std::chrono::microseconds period)
{
    loop_waiter waiter(eventLoop);
    waiter.wait(period);
}
