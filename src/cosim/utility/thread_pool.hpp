/**
*  \file
*  Slave interface.
*
*  \copyright
*      This Source Code Form is subject to the terms of the Mozilla Public
*      License, v. 2.0. If a copy of the MPL was not distributed with this
*      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_UTILITY_THREAD_POOL_HPP
#define COSIM_UTILITY_THREAD_POOL_HPP

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>

namespace cosim
{
namespace utility
{

class thread_pool
{
private:
    std::atomic_bool done_;
    std::queue<std::function<void()>> work_queue_;
    std::vector<std::thread> threads_;
    std::mutex m_;
    std::condition_variable cv_;

    void worker_thread()
    {
        while (!done_) {

            std::unique_lock<std::mutex> lck(m_);
            if (!work_queue_.empty()) {

                auto task = work_queue_.front();
                work_queue_.pop();
                task();

                lck.unlock();
                cv_.notify_one();

            } else {
                std::this_thread::yield();
            }
        }
    }

public:
    explicit thread_pool(unsigned int thread_count = std::thread::hardware_concurrency())
        : done_(false)
    {
        try {
            for (unsigned i = 0; i < thread_count; ++i) {
                threads_.emplace_back(&thread_pool::worker_thread, this);
            }
        } catch (...) {
            done_ = true;
            throw;
        }
    }

    size_t numWorkerThreads() const {
        return threads_.size();
    }

    void wait_for_tasks_to_finish()
    {
        if (!threads_.empty()) {
            std::unique_lock<std::mutex> lck(m_);
            while (!work_queue_.empty()) cv_.wait(lck);
        }
    }

    template<typename FunctionType>
    void submit(FunctionType f)
    {
        if (threads_.empty()) {
            f();
        } else {
            std::unique_lock<std::mutex> lck(m_);
            work_queue_.push(std::function<void()>(f));
        }
    }

    ~thread_pool()
    {
        done_ = true;

        for (auto& thread : threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }
};

} // namespace utility
} // namespace cosim

#endif // COSIM_UTILITY_THREAD_POOL_HPP
