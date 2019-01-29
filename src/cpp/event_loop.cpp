#include "cse/event_loop.hpp"

#include <boost/fiber/operations.hpp>

#include <utility>


namespace cse
{

namespace
{
class yield_handler : public cse::timer_event_handler
{
    void handle_timer_event(cse::timer_event*) override
    {
        boost::this_fiber::yield();
    }
};
} // namespace


event_loop_fiber::event_loop_fiber(
    std::shared_ptr<event_loop> eventLoop,
    std::chrono::microseconds yieldPeriod,
    boost::fibers::launch launchPolicy)
    : eventLoop_(eventLoop)
    , fiber_(launchPolicy, [eventLoop, yieldPeriod]() {
        yield_handler yieldHandler;
        const auto yieldTimer = eventLoop->add_timer();
        yieldTimer->enable(yieldPeriod, true, yieldHandler);
        eventLoop->loop();
        eventLoop->remove_timer(yieldTimer);
    })
{
}


event_loop_fiber::~event_loop_fiber() noexcept
{
    stop();
}


event_loop_fiber::event_loop_fiber(event_loop_fiber&& other) noexcept
    : eventLoop_(std::move(other.eventLoop_))
    , fiber_(std::move(other.fiber_))
{
}


event_loop_fiber& event_loop_fiber::operator=(event_loop_fiber&& other) noexcept
{
    std::swap(eventLoop_, other.eventLoop_);
    std::swap(fiber_, other.fiber_);
    return *this;
}


void event_loop_fiber::stop()
{
    if (fiber_.joinable()) {
        eventLoop_->stop_soon();
        fiber_.join();
    }
}


} // namespace cse
