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
    bool done_;
    // std::atomic_bool done_;
    std::queue<std::function<void()>> work_queue_;
    std::vector<std::thread> threads_;
    std::mutex m_;
    std::condition_variable cv_finished_;
    std::condition_variable cv_worker_;
    unsigned int pending_tasks_;

    void worker_thread()
    {
        while (true) {
            std::unique_lock<std::mutex> lck(m_);

            // If no work is available, block the thread here
            cv_worker_.wait(lck, [this]() { return done_ || !work_queue_.empty(); });
            if (!work_queue_.empty()) {
                pending_tasks_++;

                auto task = std::move(work_queue_.front());
                work_queue_.pop();

                lck.unlock();

                // Run work function outside mutex lock context
                task();

                lck.lock();
                pending_tasks_--;
                cv_finished_.notify_one();

                // Mutex lock goes out of scope and unlocks here, no need to call unlock() manually
            } else if (done_)
                break;
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

    [[nodiscard]] size_t numWorkerThreads() const
    {
        return threads_.size();
    }

    void wait_for_tasks_to_finish()
    {
        std::unique_lock<std::mutex> lck(m_);
        cv_finished_.wait(lck, [this]() { return work_queue_.empty() && (pending_tasks_ == 0); });
    }

    void submit(std::function<void()> f)
    {
        if (threads_.empty()) {
            f();
        } else {
            std::unique_lock<std::mutex> lck(m_);
            work_queue_.emplace(std::move(f));
            cv_worker_.notify_one();
        }
    }

    ~thread_pool() noexcept
    {
        std::unique_lock<std::mutex> lck(m_);
        done_ = true;
        cv_worker_.notify_all();
        lck.unlock();

        for (auto& thread : threads_) {
            thread.join();
        }
    }
};

} // namespace utility
} // namespace cosim

#endif // COSIM_UTILITY_THREAD_POOL_HPP
