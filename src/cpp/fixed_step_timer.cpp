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
        rtStartTime_ = startTime_;
        counter_ = 1L;
        rtCounter_ = 0L;
        realTimeFactor_ = 1.0;
    }

    void sleep()
    {
        Time::time_point current = Time::now();
        update_real_time_factor(current);
        if (realTimeSimulation_) {
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
            start();
        }
        realTimeSimulation_ = true;
    }

    void disable_real_time_simulation()
    {
        realTimeSimulation_ = false;
    }

    bool is_real_time_simulation()
    {
        return realTimeSimulation_;
    }

    double get_real_time_factor()
    {
        return realTimeFactor_;
    }


private:
    std::atomic<long> counter_ = 1L;
    long rtCounter_ = 1L;
    std::atomic<double> realTimeFactor_ = 1.0;
    std::chrono::nanoseconds stepDuration_;
    Time::time_point startTime_;
    Time::time_point rtStartTime_;
    bool realTimeSimulation_ = false;

    void update_real_time_factor(Time::time_point currentTime)
    {
        constexpr int stepsToMonitor = 5;
        if (rtCounter_ >= stepsToMonitor) {
            const std::chrono::nanoseconds expected(rtCounter_ * stepDuration_.count());
            Time::duration elapsed = currentTime - rtStartTime_;
            realTimeFactor_ = expected.count() / (1.0 * elapsed.count());
            rtStartTime_ = currentTime;
            rtCounter_ = 0L;
        }
        rtCounter_++;
    }
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

void fixed_step_timer::sleep(time_point /*currentTime*/)
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

bool fixed_step_timer::is_real_time_simulation()
{
    return pimpl_->is_real_time_simulation();
}

double fixed_step_timer::get_real_time_factor()
{
    return pimpl_->get_real_time_factor();
}


} // namespace cse
