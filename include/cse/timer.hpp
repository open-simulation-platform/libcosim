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
     * Reset start time and internal step counter. To be called when the execution is started/resumed.
     */
    virtual void start(time_point currentTime) = 0;

    /**
     * Calculates expected progress as well as elapsed time using system clock.
     * Calls thread sleep for the amount of time it would take to keep real time.
     * To be called at the tail end of each execution step.
     */
    virtual void sleep(time_point currentTime, time_duration stepSize) = 0;

    /// Enables real time simulation
    virtual void enable_real_time_simulation() = 0;

    /// Disables real time simulation
    virtual void disable_real_time_simulation() = 0;

    virtual ~real_time_timer() noexcept = default;
};

class fixed_step_timer : public real_time_timer
{
public:
    /**
     * Creates a real_time_timer based on stepSize
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

private:
    class impl;
    std::unique_ptr<impl> pimpl_;
};

} // namespace cse
#endif //CSECORE_TIMER_H
