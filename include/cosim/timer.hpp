/**
 *  \file
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef LIBCOSIM_TIMER_H
#define LIBCOSIM_TIMER_H

#include <cosim/time.hpp>

#include <boost/functional/hash.hpp>

#include <atomic>
#include <memory>

namespace cosim
{

/// A struct containing real time execution configuration.
struct real_time_config
{
    /// Real-time-synchronized simulation on or off.
    std::atomic<bool> real_time_simulation = false;

    /**
     * Real time factor target used for real-time-synchronized simulation.
     * Values smaller than or equal to zero will disable real-time synchronization.
     */
    std::atomic<double> real_time_factor_target = 1.0;

    /**
     * The number of steps used in the rolling average real time factor calculation.
     * This value is used for monitoring purposes only.
     */
    std::atomic<int> steps_to_monitor = 5;
};

} // namespace cosim

// Specialisations of std::hash for real_time_config
namespace std
{

template<>
class hash<cosim::real_time_config>
{
public:
    std::size_t operator()(const cosim::real_time_config& v) const noexcept
    {
        std::size_t seed = 0;
        boost::hash_combine(seed, v.real_time_simulation);
        boost::hash_combine(seed, v.real_time_factor_target);
        boost::hash_combine(seed, v.steps_to_monitor);
        return seed;
    }
};

} // namespace std

namespace cosim
{

/// A struct containing real time metrics.
struct real_time_metrics
{
    /// The current rolling average real time factor measurement.
    std::atomic<double> rolling_average_real_time_factor = 1.0;
    /// The total average real time factor measurement since the simulation was started.
    std::atomic<double> total_average_real_time_factor = 1.0;
};

/**
 *  A class for controlling real-time execution.
 */
class real_time_timer
{
public:
    /**
     * Reset the timer. To be called when the execution is started/resumed.
     *
     * \param [in] currentTime The current simulation time.
    */
    void start(time_point currentTime);

    /**
     * Calls thread sleep for the amount of time it would take to keep real time.
     *
     * If real time simulation is enabled, expected progress as well as elapsed time
     * are calculated. Thread sleep is called for the amount of time it would take
     * to synchronize against real time.
     *
     * To be called at the tail end of each execution step.
     *
     * \param [in] currentTime The current simulation time.
     */
    void sleep(time_point currentTime);

    std::shared_ptr<real_time_config> get_real_time_config() const;

    /// Returns a pointer to an object containing real time metrics
    std::shared_ptr<const real_time_metrics> get_real_time_metrics() const;

    /// Constructor
    real_time_timer();
    ~real_time_timer() noexcept;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace cosim
#endif //LIBCOSIM_TIMER_H
