/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/timer.hpp"

#include <chrono>
#include <thread>

typedef std::chrono::steady_clock Time;
constexpr std::chrono::microseconds MIN_SLEEP(100);

namespace cosim
{

class real_time_timer::impl
{
public:
    impl()
        : config_(std::make_shared<real_time_config>())
        , configHashValue_(std::hash<real_time_config>()(*config_))
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
        const auto newHash = std::hash<real_time_config>()(*config_);
        if (newHash != configHashValue_) {
            start(currentTime);
            configHashValue_ = newHash;
        }
        double rtfTarget = config_->real_time_factor_target.load();
        if (config_->real_time_simulation && rtfTarget > 0.0) {
            const auto elapsed = Time::now() - startTime_;
            const auto expectedSimulationTime = (currentTime - simulationStartTime_) / rtfTarget;
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
    size_t configHashValue_;
    std::shared_ptr<real_time_metrics> metrics_;

    void update_real_time_factor(Time::time_point currentTime, time_point currentSimulationTime)
    {
        const auto relativeSimTime = currentSimulationTime - simulationStartTime_;
        const auto relativeRealTime = currentTime - startTime_;
        metrics_->total_average_real_time_factor = relativeSimTime.count() / (1.0 * relativeRealTime.count());

        if (rtCounter_ >= config_->steps_to_monitor.load()) {
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
