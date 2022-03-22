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
    std::condition_variable cv_finished_;
    std::condition_variable cv_worker_;
    unsigned int pending_tasks_;

    void worker_thread()
    {
        std::chrono::seconds wait_duration(1);
        while (!done_) {
            std::unique_lock<std::mutex> lck(m_);
            if (!work_queue_.empty()) {
                auto task = std::move(work_queue_.front());
                work_queue_.pop();

                lck.unlock();
                task();

                lck.lock();
                pending_tasks_--;
                lck.unlock();
                cv_finished_.notify_one();
            } else {
                cv_worker_.wait_for(lck, wait_duration);
            }
        }
    }

public:
    explicit thread_pool(unsigned int thread_count = std::thread::hardware_concurrency())
        : done_(false)
        , pending_tasks_(0)
    {
        thread_count = std::min(thread_count, std::thread::hardware_concurrency());
        try {
            for (unsigned i = 0; i < thread_count; ++i) {
                threads_.emplace_back(&thread_pool::worker_thread, this);
            }
        } catch (...) {
            done_ = true;
            throw;
        }
    }

    thread_pool(const thread_pool&) = delete;
    thread_pool(const thread_pool&&) = delete;

    size_t numWorkerThreads() const
    {
        return threads_.size();
    }

    void wait_for_tasks_to_finish()
    {
        if (!threads_.empty()) {
            std::unique_lock<std::mutex> lck(m_);
            while (pending_tasks_ > 0) cv_finished_.wait(lck);
        }
    }

    void submit(std::function<void()> f)
    {
        if (threads_.empty()) {
            f();
        } else {
            std::unique_lock<std::mutex> lck(m_);
            work_queue_.emplace(std::move(f));
            pending_tasks_++;
            cv_worker_.notify_one();
        }
    }

    ~thread_pool() noexcept
    {
        done_ = true;
        cv_worker_.notify_all();
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
