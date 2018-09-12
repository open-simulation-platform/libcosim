#define BOOST_TEST_MODULE libevent unittests
#include <boost/test/unit_test.hpp>
#include <cse/libevent.hpp>

#include <chrono>
#include <functional>

using namespace std::chrono_literals;
using namespace cse;


namespace
{
class timer_event_function : public timer_event_handler
{
public:
    timer_event_function(
        event_loop& eventLoop,
        std::chrono::microseconds interval,
        bool persist,
        std::function<void(timer_event*)> handler)
        : handler_(handler)
    {
        eventLoop.add_timer()->enable(interval, persist, *this);
    }

    ~timer_event_function() = default;

    timer_event_function(const timer_event_function&) = delete;
    timer_event_function& operator=(const timer_event_function&) = delete;
    timer_event_function(timer_event_function&&) = delete;
    timer_event_function& operator=(timer_event_function&&) = delete;

    void handle_timer_event(timer_event* event) override
    {
        handler_(event);
    }

private:
    std::function<void(timer_event*)> handler_;
};

template<typename T, typename D>
bool approx_equal_duration(T t1, T t2, D dur)
{
    constexpr auto epsilon = 10ms;
    const auto diff = t2 - t1 - dur;
    return -epsilon <= diff && diff <= epsilon;
}
} // namespace


BOOST_AUTO_TEST_CASE(libevent_timers)
{
    using time_point = std::chrono::steady_clock::time_point;

    constexpr auto delayedDuration = 200ms;
    constexpr auto recurringDuration = 100ms;

    time_point immediateTriggered;
    time_point delayedTriggered;
    std::vector<time_point> recurringTriggered;

    const auto eventLoop = cse::make_libevent_event_loop();
    timer_event_function immediate(
        *eventLoop,
        0us,
        false,
        [&](timer_event*) { immediateTriggered = std::chrono::steady_clock::now(); });
    timer_event_function delayed(
        *eventLoop,
        delayedDuration,
        false,
        [&](timer_event*) { delayedTriggered = std::chrono::steady_clock::now(); });
    timer_event_function recurring(
        *eventLoop,
        recurringDuration,
        true,
        [&](timer_event* e) {
            recurringTriggered.push_back(std::chrono::steady_clock::now());
            if (recurringTriggered.size() == 3) e->event_loop().stop_soon();
        });
    const auto startTime = std::chrono::steady_clock::now();
    auto stopped = eventLoop->loop();

    BOOST_TEST(stopped);
    BOOST_TEST(approx_equal_duration(startTime, immediateTriggered, 0ms));
    BOOST_TEST(approx_equal_duration(startTime, delayedTriggered, delayedDuration));
    BOOST_TEST(recurringTriggered.size() == 3);
    for (std::size_t i = 0; i < recurringTriggered.size(); ++i) {
        BOOST_TEST(approx_equal_duration(startTime, recurringTriggered[i], long(i + 1) * recurringDuration));
    }
}
