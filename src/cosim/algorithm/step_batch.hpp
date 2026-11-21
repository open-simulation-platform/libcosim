/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_ALGORITHM_STEP_BATCH_HPP
#define COSIM_ALGORITHM_STEP_BATCH_HPP

#include "cosim/algorithm/simulator.hpp"
#include "cosim/error.hpp"
#include "cosim/exception.hpp"
#include "cosim/utility/thread_pool.hpp"

#include <mutex>
#include <optional>
#include <sstream>
#include <utility>

namespace cosim
{

template<typename ClockRouter>
void check_step_usable(const ClockRouter& clocks, bool initializationFailed, bool recoveryRequired)
{
    if (initializationFailed) {
        throw error(make_error_code(errc::simulation_error),
            "The algorithm cannot continue after failed initialization; recreate the execution");
    }
    clocks.check_recovery();
    if (recoveryRequired) {
        throw error(make_error_code(errc::simulation_error),
            "The algorithm cannot continue after a failed step; restore a consistent execution snapshot or recreate the execution");
    }
}

inline time_point step_endpoint(const step_result_info& result, time_point requestedEnd)
{
    return result.early_return ? result.last_successful_time : requestedEnd;
}

inline bool save_step_checkpoint(simulator& sim, bool canSaveState,
    duration step, std::optional<simulator::state_index>& checkpoint)
{
    if (!canSaveState || !sim.apply_pending_step_inputs(step)) {
        return false;
    }
    if (checkpoint) {
        sim.save_state(*checkpoint);
    } else {
        checkpoint = sim.save_state();
    }
    return true;
}

// Each algorithm chooses its participants and step sizes. This scope owns the
// worker diagnostics until all submitted calls have finished.
class step_batch
{
public:
    explicit step_batch(utility::thread_pool& pool)
        : pool_(pool)
    {
    }

    step_batch(const step_batch&) = delete;
    step_batch& operator=(const step_batch&) = delete;
    step_batch(step_batch&&) = delete;
    step_batch& operator=(step_batch&&) = delete;

    ~step_batch() noexcept
    {
        if (!joined_) {
            // Dispatch was interrupted. Wait for workers that still use this
            // scope, and let the original error propagate.
            static_cast<void>(pool_.drain_tasks());
        }
    }

    void submit(simulator& sim, step_result_info& result, time_point current, duration step)
    {
        submit(sim, [&sim, &result, current, step] {
            result = sim.do_step_with_info(current, step);
            if (result.result != step_result::complete) {
                throw error(make_error_code(errc::simulation_error), "Step not complete");
            }
        });
    }

    template<typename Task>
    void submit(const simulator& sim, Task&& task)
    {
        pool_.submit([this, &sim, run = std::forward<Task>(task)]() mutable {
            try {
                run();
            } catch (const std::exception& e) {
                record_failure(sim, e.what());
            } catch (...) {
                record_failure(sim, "Unknown exception while stepping");
            }
        });
    }

    void join()
    {
        joined_ = true;
        pool_.wait_for_tasks_to_finish();
        if (failed_) {
            throw error(make_error_code(errc::simulation_error), errors_.str());
        }
    }

private:
    void record_failure(const simulator& sim, const char* message)
    {
        std::scoped_lock const lock(mutex_);
        errors_ << sim.name() << ": " << message << '\n';
        failed_ = true;
    }

    utility::thread_pool& pool_;
    std::mutex mutex_;
    std::stringstream errors_;
    bool failed_ = false;
    bool joined_ = false;
};

} // namespace cosim

#endif // COSIM_ALGORITHM_STEP_BATCH_HPP
