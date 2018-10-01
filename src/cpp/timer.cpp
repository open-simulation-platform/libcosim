#include <cse/timer.hpp>
#include <cse/log.hpp>
#include <cse/log/logger.hpp>

typedef std::chrono::steady_clock Time;
constexpr std::chrono::duration<long, std::nano> MIN_SLEEP(100000L);

namespace cse
{

real_time_timer::real_time_timer(cse::time_duration stepSize)
{
    long nanos = static_cast<long>(stepSize * 1e9);
    stepDuration = std::chrono::duration<long>(nanos);
}

void real_time_timer::start()
{
    startTime = Time::now();
    counter = 1L;
}

void real_time_timer::sleep()
{
    Time::time_point current = Time::now();
    Time::duration elapsed = current - startTime;

    const std::chrono::duration<long, std::nano> expected(counter * stepDuration.count());
    const std::chrono::duration<long, std::nano> totalSleep = expected - elapsed;

    if (totalSleep > MIN_SLEEP) {
        BOOST_LOG_SEV(log::logger::get(), log::level::trace)
                << "Real time timer sleeping for "
                << (std::chrono::duration_cast<std::chrono::milliseconds>(totalSleep)).count()
                << " ms";

        std::this_thread::sleep_for(totalSleep);
    } else {
        BOOST_LOG_SEV(log::logger::get(), log::level::debug)
            << "Real time timer NOT sleeping, calculated sleep time "
            << (std::chrono::duration_cast<std::chrono::nanoseconds>(totalSleep)).count() <<
            " ns";

    }

    counter++;
}
} // namespace cse
