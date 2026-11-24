/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef LIBCOSIM_ALGORITHM_EVENT_ITERATION_BUDGET_HPP
#define LIBCOSIM_ALGORITHM_EVENT_ITERATION_BUDGET_HPP

#include "cosim/error.hpp"
#include "cosim/event_iteration.hpp"
#include "cosim/exception.hpp"
#include "cosim/log/logger.hpp"
#include "cosim/serialization.hpp"
#include "cosim/time.hpp"

#include <boost/property_tree/exceptions.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace cosim
{

/**
 * Shared per-physical-time budget for coupled event rounds and zero-progress
 * steps. Both counters reset at a later time and are included in snapshots.
 * The owner must restore a snapshot before coordinating at an earlier time.
 */
class event_iteration_budget
{
public:
    /// Sets a positive limit before any coordination has begun.
    void set_limit(std::size_t limit)
    {
        COSIM_INPUT_CHECK(limit > 0);
        if (time_) {
            throw error(make_error_code(errc::simulation_error),
                "The event iteration limit cannot be changed after coordination starts");
        }
        limit_ = limit;
    }

    /// Returns the configured limit without consuming a coordination round.
    [[nodiscard]] std::size_t limit() const noexcept { return limit_; }

    [[nodiscard]] std::size_t rounds_used() const noexcept { return iterations_; }

    /// Starts a physical time or resumes its existing budget.
    void begin(time_point time)
    {
        if (time_ && time < *time_) {
            throw error(make_error_code(errc::simulation_error),
                "Event coordination time cannot move backwards without restoring state");
        }
        if (!time_ || time > *time_) {
            time_ = time;
            iterations_ = 0;
            zero_progress_steps_ = 0;
            warned_ = false;
        }
    }

    /// Reserves one coupled round, returning false when the limit is exhausted.
    bool acquire(time_point time)
    {
        begin(time);
        if (iterations_ == limit_) {
            return false;
        }
        ++iterations_;
        return true;
    }

    /// Reserves one zero-progress step independently of the coupled-round count.
    bool acquire_zero_progress_step(time_point time)
    {
        begin(time);
        if (zero_progress_steps_ == limit_) {
            return false;
        }
        ++zero_progress_steps_;
        return true;
    }

    /// Logs the non-convergence warning at most once at this time.
    void warn(time_point time, const std::string& details = {})
    {
        begin(time);
        if (warned_) {
            return;
        }
        iterations_ = limit_;
        BOOST_LOG_SEV(log::logger(), log::warning)
            << "Coupled events at time " << to_double_time_point(time)
            << " s did not converge within " << limit_
            << " rounds; continuing with unstabilized outputs and deferred "
               "inputs; dropped undelivered Clock ticks, if any"
            << details;
        warned_ = true;
    }

    /// Serializes the current time, counters, limit and warning state.
    [[nodiscard]] serialization::node export_state() const
    {
        serialization::node state;
        state.put<std::uint32_t>("version", 2);
        state.put<std::uint64_t>("limit", limit_);
        state.put<std::uint64_t>("iterations", iterations_);
        state.put<std::uint64_t>("zero_progress_steps", zero_progress_steps_);
        state.put("warned", warned_);
        state.put("has_time", time_.has_value());
        if (time_) {
            state.put<std::int64_t>("time_ticks", time_->time_since_epoch().count());
        }
        return state;
    }

    /// Restores a compatible snapshot or throws for corrupt or mismatched state.
    void import_state(const serialization::node& state)
    {
        try {
            const auto version = state.get<std::uint32_t>("version");
            const auto limit = state.get<std::uint64_t>("limit");
            const auto iterations = state.get<std::uint64_t>("iterations");
            const auto zeroProgressSteps = version == 1
                ? 0
                : state.get<std::uint64_t>("zero_progress_steps");
            const auto warned = state.get<bool>("warned");
            const auto hasTime = state.get<bool>("has_time");
            std::optional<time_point> time;
            if (hasTime) {
                time = time_point(duration(state.get<std::int64_t>("time_ticks")));
            }
            if ((version != 1 && version != 2) || limit != limit_ ||
                iterations > limit || zeroProgressSteps > limit ||
                (!hasTime && (iterations != 0 || zeroProgressSteps != 0 || warned)) ||
                (warned && iterations != limit)) {
                throw error(make_error_code(errc::bad_file),
                    "The serialized event coordination state is invalid or incompatible");
            }
            time_ = time;
            iterations_ = static_cast<std::size_t>(iterations);
            zero_progress_steps_ = static_cast<std::size_t>(zeroProgressSteps);
            warned_ = warned;
        } catch (const boost::property_tree::ptree_error&) {
            throw error(make_error_code(errc::bad_file),
                "The serialized event coordination state is invalid or corrupt");
        } catch (const std::bad_variant_access&) {
            throw error(make_error_code(errc::bad_file),
                "The serialized event coordination state is invalid or corrupt");
        }
    }

    /// Clears coordination history while retaining the configured limit.
    void reset()
    {
        time_.reset();
        iterations_ = 0;
        zero_progress_steps_ = 0;
        warned_ = false;
    }

private:
    std::optional<time_point> time_;
    std::size_t limit_ = default_max_event_iterations;
    std::size_t iterations_ = 0;
    std::size_t zero_progress_steps_ = 0;
    bool warned_ = false;
};

} // namespace cosim

#endif
