#include <algorithm>
#include <chrono>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

#include <boost/fiber/all.hpp>
#include <gsl/gsl_util>

#include <cse/algorithm.hpp>
#include <cse/execution.hpp>
#include <cse/event_loop.hpp>
#include <cse/libevent.hpp>

#include "mock_slave.hpp"


// This class is to make the event loop pass control to Boost's fiber scheduler
// every now and then.
class yield_handler : public cse::timer_event_handler
{
    void handle_timer_event(cse::timer_event*) override
    {
        boost::this_fiber::yield();
        int i = 1; ++i;
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
    std::shared_ptr<cse::event_loop> eventLoop = cse::make_libevent_event_loop();
    yield_handler yieldHandler;
    eventLoop->add_timer()->enable(std::chrono::milliseconds(1), true, yieldHandler);

    // Spin off the event loop in a separate fiber.
    auto eventLoopFiber = fiber([eventLoop]() {
        eventLoop->loop();
        int i = 1; ++i;
    });
    auto finalizeEventLoop = gsl::finally([&] () {
        eventLoop->stop_soon();
        eventLoopFiber.join();
    });

    try {
        constexpr int numSlaves = 10;
        constexpr cse::time_point startTime = 0.0;
        constexpr cse::time_point midTime = 0.6;
        constexpr cse::time_point endTime = 1.0;
        constexpr cse::time_duration stepSize = 0.1;

        // Set up execution
        auto execution = cse::execution(
            startTime,
            std::make_unique<cse::fixed_step_algorithm>(stepSize));

        // Add slaves to it
        for (int i = 0; i < numSlaves; ++i) {
            execution.add_slave(
                make_async_slave<mock_slave>(eventLoop),
                "slave" + std::to_string(i));
        }

        // Run simulation
        auto simResult = execution.simulate_until(midTime);
        REQUIRE(simResult.get());
        REQUIRE(execution.current_time() == midTime);
        simResult = execution.simulate_until(endTime);
        REQUIRE(simResult.get());

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
