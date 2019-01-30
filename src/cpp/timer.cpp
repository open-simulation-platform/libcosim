#include "cse/timer.hpp"

#include "cse/log.hpp"
#include "cse/log/logger.hpp"

#include <atomic>
#include <chrono>
#include <thread>


typedef std::chrono::steady_clock Time;
constexpr std::chrono::microseconds MIN_SLEEP(100);

namespace cse
{


class real_time_timer::impl
{
public:
    impl()
    {}

    void start(time_point currentTime)
    {
        simulationStartTime_ = currentTime;
        rtSimulationStartTime_ = currentTime;
        startTime_ = Time::now();
        rtStartTime_ = startTime_;
        rtCounter_ = 0L;
        realTimeFactor_ = 1.0;
    }

    void sleep(time_point currentTime)
    {
        Time::time_point current = Time::now();
        update_real_time_factor(current, currentTime);
        lastSimulationTime_ = currentTime;
        if (realTimeSimulation_) {
            Time::duration elapsed = current - startTime_;
            const duration expectedSimulationTime = currentTime - simulationStartTime_;
            const auto expected = std::chrono::duration_cast<std::chrono::nanoseconds>(expectedSimulationTime);
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
        }
    }

    void enable_real_time_simulation()
    {
        if (!realTimeSimulation_) {
            start(lastSimulationTime_);
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
    long rtCounter_ = 0L;
    std::atomic<double> realTimeFactor_ = 1.0;
    std::atomic<bool> realTimeSimulation_ = false;
    Time::time_point startTime_;
    Time::time_point rtStartTime_;
    time_point simulationStartTime_;
    time_point rtSimulationStartTime_;
    time_point lastSimulationTime_;

    void update_real_time_factor(Time::time_point currentTime, time_point currentSimulationTime)
    {
        constexpr int stepsToMonitor = 5;
        if (rtCounter_ >= stepsToMonitor) {

            const duration expectedSimulationTime = currentSimulationTime - rtSimulationStartTime_;
            const auto expected = std::chrono::duration_cast<std::chrono::nanoseconds>(expectedSimulationTime);

            Time::duration elapsed = currentTime - rtStartTime_;
            realTimeFactor_ = expected.count() / (1.0 * elapsed.count());
            rtStartTime_ = currentTime;
            rtSimulationStartTime_ = currentSimulationTime;
            rtCounter_ = 0L;
        }
        rtCounter_++;
    }
};

real_time_timer::real_time_timer()
    : pimpl_(std::make_unique<impl>())
{
}

real_time_timer::~real_time_timer() noexcept = default;

void real_time_timer::start(time_point currentTime)
{
    pimpl_->start(currentTime);
}

void real_time_timer::sleep(time_point currentTime)
{
    pimpl_->sleep(currentTime);
}

void real_time_timer::enable_real_time_simulation()
{
    pimpl_->enable_real_time_simulation();
}

void real_time_timer::disable_real_time_simulation()
{
    pimpl_->disable_real_time_simulation();
}

bool real_time_timer::is_real_time_simulation()
{
    return pimpl_->is_real_time_simulation();
}

double real_time_timer::get_real_time_factor()
{
    return pimpl_->get_real_time_factor();
}


} // namespace cse
