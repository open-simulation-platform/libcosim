#include <cse/log.hpp>
#include <cse/log/logger.hpp>
#include <cse/timer.hpp>


typedef std::chrono::steady_clock Time;
constexpr std::chrono::microseconds MIN_SLEEP(100);

namespace cse
{

real_time_timer::real_time_timer(cse::time_duration stepSize)
    : stepDuration_(static_cast<long long>(stepSize * 1e9))
{
}

void real_time_timer::start()
{
    startTime_ = Time::now();
    counter_ = 1L;
}

void real_time_timer::sleep()
{
    Time::time_point current = Time::now();
    Time::duration elapsed = current - startTime_;

    const std::chrono::nanoseconds expected(counter_ * stepDuration_.count());
    const std::chrono::nanoseconds totalSleep = expected - elapsed;

    if (totalSleep > MIN_SLEEP) {
        BOOST_LOG_SEV(log::logger(), log::level::trace)
            << "Real time timer sleeping for "
            << (std::chrono::duration_cast<std::chrono::milliseconds>(totalSleep)).count()
            << " ms";

        std::this_thread::sleep_for(totalSleep);
    } else {
        BOOST_LOG_SEV(log::logger(), log::level::debug)
            << "Real time timer NOT sleeping, calculated sleep time "
            << totalSleep.count() << " ns";
    }

    counter_++;
}
} // namespace cse
