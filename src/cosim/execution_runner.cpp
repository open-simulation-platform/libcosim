/*
*  This Source Code Form is subject to the terms of the Mozilla Public
*  License, v. 2.0. If a copy of the MPL was not distributed with this
*  file, You can obtain one at https://mozilla.org/MPL/2.0/.
*/
#include "cosim/execution_runner.hpp"

#include <future>
#include <atomic>

namespace cosim
{

class execution_runner::impl
{

public:
    explicit impl(execution& exec)
        : exec_(exec)
        , stopped_(true)
        , timer_()
    { }

    impl(const impl&) = delete;
    impl& operator=(const impl&) = delete;

    impl(impl&&) = delete;
    impl& operator=(impl&&) = delete;

    [[nodiscard]] bool is_running() const noexcept
    {
        return !stopped_;
    }

    void stop_simulation()
    {
        stopped_ = true;
    }

    std::future<bool> simulate_until(std::optional<time_point> endTime)
    {
        if (is_running()) {
            throw std::logic_error("Simulation is already running!");
        }

        promise_ = std::promise<bool>{};
        stopped_ = false;
        if (t_.joinable()) t_.join();
        t_ = std::thread([this, endTime]{
            timer_.start(exec_.current_time());
            duration stepSize;
            do {
                stepSize = exec_.step();
                timer_.sleep(exec_.current_time());
            } while (!stopped_ && !timed_out(endTime, exec_.current_time(), stepSize));
            bool isStopped = stopped_;
            stopped_ = true;
            promise_.set_value(!isStopped);
        });
        return promise_.get_future();
    }

    [[nodiscard]] std::shared_ptr<real_time_config> get_real_time_config() const
    {
        return timer_.get_real_time_config();
    }

    [[nodiscard]] std::shared_ptr<const real_time_metrics> get_real_time_metrics() const
    {
        return timer_.get_real_time_metrics();
    }

    ~impl() {
        if (t_.joinable()) {
            stopped_ = true;
            t_.join();
        }
    };

private:
    execution& exec_;

    std::thread t_;
    std::atomic<bool> stopped_;
    std::promise<bool> promise_;
    real_time_timer timer_;

    static bool timed_out(std::optional<time_point> endTime, time_point currentTime, duration stepSize)
    {
        constexpr double relativeTolerance = 0.01;
        if (endTime) {
            return *endTime - currentTime < stepSize * relativeTolerance;
        }
        return false;
    }
};

execution_runner::execution_runner(execution& exec)
    : pimpl_(std::make_unique<execution_runner::impl>(exec))
{ }

execution_runner::~execution_runner() noexcept = default;
execution_runner::execution_runner(execution_runner&& other) noexcept = default;
execution_runner& execution_runner::operator=(execution_runner&& other) noexcept = default;

std::future<bool> execution_runner::simulate_until(std::optional<time_point> endTime)
{
    return pimpl_->simulate_until(endTime);
}


bool execution_runner::is_running() const noexcept
{
    return pimpl_->is_running();
}

void execution_runner::stop_simulation()
{
    pimpl_->stop_simulation();
}

std::shared_ptr<real_time_config> execution_runner::get_real_time_config() const
{
    return pimpl_->get_real_time_config();
}

std::shared_ptr<const real_time_metrics> execution_runner::get_real_time_metrics() const
{
    return pimpl_->get_real_time_metrics();
}


} // namespace cosim
