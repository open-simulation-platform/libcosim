/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/timer.hpp"

#include "cosim/error.hpp"

#include <chrono>
#include <thread>

typedef std::chrono::steady_clock Time;
constexpr std::chrono::microseconds MIN_SLEEP(100);

namespace cosim
{

void real_time_config::set_real_time_simulation(bool enable)
{
    if (realTimeSimulation_ != enable) {
        configChanged_.store(true);
        realTimeSimulation_.store(enable);
    }
}

bool real_time_config::is_real_time_simulation() const
{
    return realTimeSimulation_.load();
}

void real_time_config::set_real_time_factor_target(double factor)
{
    COSIM_INPUT_CHECK(factor > 0.0);
    if (realTimeFactorTarget_.load() != factor) {
        configChanged_.store(true);
        realTimeFactorTarget_.store(factor);
    }
}

double real_time_config::get_real_time_factor_target() const
{
    return realTimeFactorTarget_.load();
}

void real_time_config::set_steps_to_monitor(int steps)
{
    COSIM_INPUT_CHECK(steps > 0);
    if (stepsToMonitor_.load() != steps) {
        configChanged_.store(true);
        stepsToMonitor_.store(steps);
    }
}

int real_time_config::get_steps_to_monitor() const
{
    return stepsToMonitor_.load();
}


class real_time_timer::impl
{
public:
    impl()
        : config_(std::make_shared<real_time_config>())
        , metrics_(std::make_shared<real_time_metrics>())
    {}

    void start(time_point currentTime)
    {
        simulationStartTime_ = currentTime;
        rtSimulationStartTime_ = currentTime;
        startTime_ = Time::now();
        rtStartTime_ = startTime_;
        rtCounter_ = 0L;
    }

    void sleep(time_point currentTime)
    {
        if (config_->configChanged_.load()) {
            start(currentTime);
            config_->configChanged_.store(false);
        }
        if (config_->is_real_time_simulation()) {
            const auto elapsed = Time::now() - startTime_;
            const auto expectedSimulationTime = (currentTime - simulationStartTime_) / config_->realTimeFactorTarget_.load();
            const auto simulationSleepTime = expectedSimulationTime - elapsed;

            if (simulationSleepTime > MIN_SLEEP) {
                std::this_thread::sleep_for(simulationSleepTime);
            }
        }
        update_real_time_factor(Time::now(), currentTime);
    }

    std::shared_ptr<real_time_config> get_real_time_config() const
    {
        return config_;
    }

    std::shared_ptr<const real_time_metrics> get_real_time_metrics() const
    {
        return metrics_;
    }


private:
    long rtCounter_ = 0L;
    Time::time_point startTime_;
    Time::time_point rtStartTime_;
    time_point simulationStartTime_;
    time_point rtSimulationStartTime_;
    std::shared_ptr<real_time_config> config_;
    std::shared_ptr<real_time_metrics> metrics_;

    void update_real_time_factor(Time::time_point currentTime, time_point currentSimulationTime)
    {
        const auto relativeSimTime = currentSimulationTime - simulationStartTime_;
        const auto relativeRealTime = currentTime - startTime_;
        metrics_->total_average_real_time_factor = relativeSimTime.count() / (1.0 * relativeRealTime.count());

        if (rtCounter_ >= config_->stepsToMonitor_.load()) {
            const auto elapsedSimTime = currentSimulationTime - rtSimulationStartTime_;
            const auto elapsedRealTime = currentTime - rtStartTime_;

            metrics_->rolling_average_real_time_factor = elapsedSimTime.count() / (1.0 * elapsedRealTime.count());
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

std::shared_ptr<real_time_config> real_time_timer::get_real_time_config() const
{
    return pimpl_->get_real_time_config();
}

std::shared_ptr<const real_time_metrics> real_time_timer::get_real_time_metrics() const
{
    return pimpl_->get_real_time_metrics();
}

} // namespace cosim
