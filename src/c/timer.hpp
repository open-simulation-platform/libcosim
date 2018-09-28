#ifndef CSECORE_TIMER_H
#define CSECORE_TIMER_H

#include <atomic>
#include <chrono>
#include <thread>

#include <cse/model.hpp>



namespace cse
{

class real_time_timer
{

public:
    real_time_timer(cse::time_duration stepSize);
    void start();
    void sleep();

private:

    std::atomic<long> counter;
    std::chrono::duration<long> stepDuration;
    std::chrono::steady_clock::time_point startTime;

};

} // namespace cse
#endif //CSECORE_TIMER_H
