/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef LIBCOSIM_EXECUTION_RUNNER_HPP
#define LIBCOSIM_EXECUTION_RUNNER_HPP

#include "execution.hpp"

#include <future>

namespace cosim
{

class execution_runner
{

public:
    explicit execution_runner(execution& exec);

    ~execution_runner();

    execution_runner(const execution_runner&) = delete;
    execution_runner& operator=(const execution_runner&) = delete;

    execution_runner(execution_runner&&) noexcept;
    execution_runner& operator=(execution_runner&&) noexcept;

    /**
     *  Advance the co-simulation forward to the given logical time.
     *
     *  \param targetTime
     *      The logical time at which the co-simulation should pause (optional).
     *      If specified, this must always be greater than the value of
     *      `current_time()` at the moment the function is called. If not specified,
     *      the co-simulation will continue until `stop_simulation()` is called.
     *
     *  \returns
     *      `true` if the co-simulation was advanced to the given time,
     *      or `false` if it was stopped before this. In the latter case,
     *      `current_time()` may be called to determine the actual end time.
     */
    std::future<bool> simulate_until(std::optional<time_point> targetTime);

    /// Stops the co-simulation temporarily.
    void stop_simulation();

    /// Is the simulation loop currently running
    [[nodiscard]] bool is_running() const noexcept;

    /// Returns a pointer to an object containing real time configuration
    [[nodiscard]] std::shared_ptr<real_time_config> get_real_time_config() const;

    /// Returns a pointer to an object containing real time metrics
    [[nodiscard]] std::shared_ptr<const real_time_metrics> get_real_time_metrics() const;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace cosim

#endif // LIBCOSIM_EXECUTION_RUNNER_HPP
