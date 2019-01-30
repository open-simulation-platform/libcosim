/**
 *  \file
 *  \brief Concurrency utilities.
 */
#ifndef CSE_UTILITY_CONCURRENCY
#define CSE_UTILITY_CONCURRENCY

#include <boost/fiber/condition_variable.hpp>
#include <boost/fiber/mutex.hpp>

#include <mutex>
#include <optional>


namespace cse
{
namespace utility
{


/**
 *  A thread-safe, fiber-friendly, single-item container.
 *
 *  This is a general-purpose container that may contain zero or one item(s)
 *  of type `T`.
 *
 *  The `put()` and `take()` functions can be safely called from different
 *  threads.  Shared data is protected with synchronization primitives
 *  from Boost.Fiber, meaning that the functions will yield to other fibers,
 *  and not block the current thread, if they cannot immediately acquire a
 *  lock.
 */
template<typename T>
class shared_box
{
public:
    using value_type = T;

    /// Puts an item in the container, replacing any existing item.
    void put(const value_type& value)
    {
        {
            std::lock_guard<boost::fibers::mutex> lock(mutex_);
            value_ = value;
        }
        condition_.notify_one();
    }

    /// Puts an item in the container, replacing any existing item.
    void put(value_type&& value)
    {
        {
            std::lock_guard<boost::fibers::mutex> lock(mutex_);
            value_ = std::move(value);
        }
        condition_.notify_one();
    }

    /**
     *  Removes an item from the container and returns it.
     *
     *  If there is no item in the container when the function is called,
     *  the current fiber will yield, and it will only resume when an
     *  item becomes available.
     */
    value_type take()
    {
        std::unique_lock<boost::fibers::mutex> lock(mutex_);
        condition_.wait(lock, [&] { return value_.has_value(); });
        auto value = std::move(value_.value());
        value_.reset();
        return std::move(value);
    }

    /// Returns `true` if there is no item in the container.
    bool empty() const noexcept
    {
        std::lock_guard<boost::fibers::mutex> lock(mutex_);
        return !value_.has_value();
    }

private:
    std::optional<value_type> value_;
    mutable boost::fibers::mutex mutex_;
    boost::fibers::condition_variable condition_;
};


} // namespace utility
} // namespace cse
#endif // header guard
