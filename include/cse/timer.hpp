#ifndef CSECORE_TIMER_H
#define CSECORE_TIMER_H

#include <memory>

#include <cse/model.hpp>

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
    virtual void start(time_point currentTime) = 0;

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
     * \param [in] stepSize The step size for the last performed step.
     */
    virtual void sleep(time_point currentTime, time_duration stepSize) = 0;

    /// Enables real time simulation
    virtual void enable_real_time_simulation() = 0;

    /// Disables real time simulation
    virtual void disable_real_time_simulation() = 0;

    /// Returns if this is a real time simulation
    virtual bool is_real_time_simulation() = 0;

    /// Returns the current real time factor
    virtual double get_real_time_factor() = 0;

    virtual ~real_time_timer() noexcept = default;
};

/**
 *  A real-time timer based on fixed step size.
 *
 *  The `fixed_step_timer` controls the real-time progression of a fixed step size
 *  simulation, and is suited for use with the `fixed_step_algorithm` class.
 */
class fixed_step_timer : public real_time_timer
{
public:
    /**
     * Creates a fixed step timer based on a step size.
     *
     * \param [in] stepSize
     *     The step size (in seconds) used in the execution.
     */
    explicit fixed_step_timer(time_duration stepSize);
    ~fixed_step_timer() noexcept;

    void start(time_point currentTime) override;
    void sleep(time_point currentTime, time_duration stepSize) override;
    void enable_real_time_simulation() override;
    void disable_real_time_simulation() override;
    bool is_real_time_simulation() override;
    double get_real_time_factor() override;

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace cse
#endif //CSECORE_TIMER_H
