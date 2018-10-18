#include <atomic>
#include <chrono>
#include <thread>

#include <cse/log.hpp>
#include <cse/log/logger.hpp>
#include <cse/timer.hpp>


typedef std::chrono::steady_clock Time;
constexpr std::chrono::microseconds MIN_SLEEP(100);

namespace cse
{
class fixed_step_timer::impl
{
public:
    explicit impl(time_duration stepSize)
        : stepDuration_(static_cast<long long>(stepSize * 1e9))
    {
    }

    void start()
    {
        startTime_ = Time::now();
        counter_ = 1L;
    }

    void sleep()
    {
        if (realTimeSimulation_) {
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
    }

    void enable_real_time_simulation()
    {
        if (!realTimeSimulation_) {
            startTime_ = Time::now();
            counter_ = 1L;
        }
        realTimeSimulation_ = true;
    }

    void disable_real_time_simulation()
    {
        realTimeSimulation_ = false;
    }


private:
    std::atomic<long> counter_ = 1L;
    std::chrono::nanoseconds stepDuration_;
    std::chrono::steady_clock::time_point startTime_;
    bool realTimeSimulation_ = false;
};

fixed_step_timer::fixed_step_timer(time_duration stepSize)
    : pimpl_(std::make_unique<impl>(stepSize))
{
}

fixed_step_timer::~fixed_step_timer() noexcept {}

void fixed_step_timer::start(time_point /*currentTime*/)
{
    pimpl_->start();
}

void fixed_step_timer::sleep(time_point /*currentTime*/, time_duration /*stepSize*/)
{
    pimpl_->sleep();
}

void fixed_step_timer::enable_real_time_simulation()
{
    pimpl_->enable_real_time_simulation();
}

void fixed_step_timer::disable_real_time_simulation()
{
    pimpl_->disable_real_time_simulation();
}


} // namespace cse
