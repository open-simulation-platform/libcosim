/**
 *  \file
 *  \brief Concurrency utilities.
 */
#ifndef CSE_UTILITY_CONCURRENCY
#define CSE_UTILITY_CONCURRENCY

#include <boost/fiber/condition_variable.hpp>
#include <boost/fiber/mutex.hpp>
#include <boost/filesystem.hpp>
#include <boost/interprocess/sync/file_lock.hpp>

#include <memory>
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


/**
 *  A file-based mutual exclusion mechanism.
 *
 *  This class provides interprocess synchronisation based on
 *  `boost::interprocess::file_lock`, augmenting it with support for
 *  inter-fiber and inter-thread synchronisation.  This is achieved by
 *  combining the file lock with a lock on a global `boost::fibers::mutex`
 *  object that is associated with the file.
 *
 *  Note that a single `file_lock` object may only be used by one fiber at a
 *  time.  That is, if it is locked by one fiber, it must be unlocked by the
 *  same fiber.  Other fibers may not attempt to call its locking or unlocking
 *  functions in the meantime.
 *
 *  Furthermore, once a fiber has locked a file, the same fiber may not attempt
 *  to use a different `file_lock` object to lock the same file, as this would
 *  cause a deadlock.
 *
 *  Therefore, to synchronise between fibers (including those running in
 *  separate threads), it is recommended to create one and only one `file_lock`
 *  object associated with the same file in each fiber.
 *
 *  If a lock is held upon destruction, it is automatically released.
 *
 *  The class meets the requirements of the
 *  [Lockable](https://en.cppreference.com/w/cpp/named_req/Lockable) concept.
 */
class file_lock
{
public:
    /**
     *  Constructs an object that uses the file at `path` as a lock file.
     *
     *  If the file already exists, the current process must have write
     *  permissions to it (though it will not be modified).
     *  If it does not exist, it will be created.
     *
     *  The constructor will not attempt to lock the file.
     *
     *  \throws std::system_error
     *      if the file could not be opened or created.
     */
    explicit file_lock(const boost::filesystem::path& path);

    /**
     *  Acquires a lock on the file, blocking if necessary.
     *
     *  \pre
     *      This `file_lock` object is not already locked.
     *      The file is not locked by a different `file_lock` object in the
     *      same fiber.
     */
    void lock();

    /**
     *  Attempts to acquire a lock on the file without blocking and returns
     *  whether the attempt was successful.
     *
     *  \pre
     *      This `file_lock` object is not already locked.
     *      The file is not locked by a different `file_lock` object in the
     *      same fiber.
     */
    bool try_lock();

    /**
     *  Unlocks the file.
     *
     *  \pre
     *      This `file_lock` object has been locked in the current fiber.
     */
    void unlock() noexcept;

private:
    boost::interprocess::file_lock fileLock_;
    std::shared_ptr<boost::fibers::mutex> mutex_;
    std::unique_lock<boost::fibers::mutex> mutexLock_;
};


} // namespace utility
} // namespace cse
#endif // header guard
