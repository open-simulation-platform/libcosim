#ifndef CSECORE_TIMER_H
#define CSECORE_TIMER_H

#include <cse/model.hpp>

#include <memory>

namespace cse
{

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

    /// Enables real time simulation
    void enable_real_time_simulation();

    /// Disables real time simulation
    void disable_real_time_simulation();

    /// Returns if this is a real time simulation
    bool is_real_time_simulation();

    /// Returns the current real time factor
    double get_measured_real_time_factor();

    /// Sets a custom real time factor
    void set_real_time_factor_target(double realTimeFactor);

    /// Returns the current real time factor target
    double get_real_time_factor_target();

    /// Constructor
    real_time_timer();
    ~real_time_timer() noexcept;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace cse
#endif //CSECORE_TIMER_H
