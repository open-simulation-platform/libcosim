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
#include <exception>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>


namespace cosim::utility
{

/**
 * Runs submitted tasks on a fixed set of workers, or inline with zero workers.
 * Waiting rethrows the first task exception after all pending work finishes.
 */
class thread_pool
{
private:
    bool done_ = false;
    std::queue<std::function<void()>> work_queue_;
    std::vector<std::thread> threads_;
    std::mutex m_;
    std::condition_variable cv_finished_;
    std::condition_variable cv_worker_;
    unsigned int pending_tasks_ = 0;
    std::exception_ptr task_exception_;

    void stop_workers()
    {
        {
            std::scoped_lock const lock(m_);
            done_ = true;
        }
        cv_worker_.notify_all();
        for (auto& thread : threads_) {
            thread.join();
        }
    }

    void worker_thread()
    {
        while (true) {
            std::unique_lock<std::mutex> lck(m_);

            // If no work is available, block the thread here
            cv_worker_.wait(lck, [this] { return done_ || !work_queue_.empty(); });
            if (!work_queue_.empty()) {
                pending_tasks_++;

                const auto task = std::move(work_queue_.front());
                work_queue_.pop();

                lck.unlock();

                // Run work function outside mutex lock context
                std::exception_ptr taskException;
                try {
                    task();
                } catch (...) {
                    taskException = std::current_exception();
                }

                lck.lock();
                if (taskException && !task_exception_) {
                    task_exception_ = taskException;
                }
                pending_tasks_--;
                lck.unlock();
                cv_finished_.notify_one();
            } else if (done_) {
                break;
            }
        }
    }

public:
    explicit thread_pool(unsigned int thread_count)
    {
        try {
            for (unsigned i = 0; i < thread_count; ++i) {
                threads_.emplace_back(&thread_pool::worker_thread, this);
            }
        } catch (...) {
            stop_workers();
            throw;
        }
    }

    thread_pool(const thread_pool&) = delete;
    thread_pool(thread_pool&&) = delete;
    thread_pool& operator=(const thread_pool&) = delete;
    thread_pool& operator=(thread_pool&&) = delete;

    [[nodiscard]] size_t numWorkerThreads() const noexcept
    {
        return threads_.size();
    }

    /// Waits for queued and running tasks and consumes the first failure.
    [[nodiscard]] std::exception_ptr drain_tasks()
    {
        std::unique_lock<std::mutex> lck(m_);
        cv_finished_.wait(lck, [this] { return work_queue_.empty() && (pending_tasks_ == 0); });
        return std::exchange(task_exception_, nullptr);
    }

    /// Waits for queued and running tasks and rethrows the first failure.
    void wait_for_tasks_to_finish()
    {
        if (const auto taskException = drain_tasks()) {
            std::rethrow_exception(taskException);
        }
    }

    /// Queues a task, or runs it immediately if no worker threads exist.
    void submit(std::function<void()> task)
    {
        if (threads_.empty()) {
            try {
                task();
            } catch (...) {
                const std::unique_lock<std::mutex> lock(m_);
                if (!task_exception_) {
                    task_exception_ = std::current_exception();
                }
            }
        } else {
            std::unique_lock<std::mutex> lck(m_);
            work_queue_.emplace(std::move(task));
            lck.unlock();
            cv_worker_.notify_one();
        }
    }

    ~thread_pool() noexcept
    {
        stop_workers();
    }
};

} // namespace cosim::utility


#endif // COSIM_UTILITY_THREAD_POOL_HPP
