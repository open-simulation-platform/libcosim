#ifndef CSECORE_TIMER_H
#define CSECORE_TIMER_H

#include <atomic>
#include <chrono>
#include <thread>

#include <cse/model.hpp>

namespace cse
{

/**
 *  A class for controlling real-time execution.
 */
class real_time_timer
{

public:
    /*
     * Creates a real_time_timer based on stepSize
     *
     * \param [in] stepSize
     *     The step size (in seconds) used in the execution.
     */
    real_time_timer(cse::time_duration stepSize);

    /*
     * Reset start time and internal step counter. To be called when the execution is started/resumed.
     */
    void start();

    /*
     * Calculates expected progress as well as elapsed time using system clock.
     * Calls thread sleep for the amount of time it would take to keep real time.
     * To be called at the tail end of each execution step.
     */
    void sleep();

private:
    std::atomic<long> counter = 1L;
    std::chrono::duration<long> stepDuration;
    std::chrono::steady_clock::time_point startTime;
};

} // namespace cse
#endif //CSECORE_TIMER_H
