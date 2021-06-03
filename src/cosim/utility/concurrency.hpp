/**
 *  \file
 *  Concurrency utilities.
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_UTILITY_CONCURRENCY
#define COSIM_UTILITY_CONCURRENCY

#include <cosim/fs_portability.hpp>

#include <boost/fiber/condition_variable.hpp>
#include <boost/fiber/mutex.hpp>
#include <boost/interprocess/sync/file_lock.hpp>

#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <variant>


namespace cosim
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
        return value;
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
 *  A shared mutex à la `std::shared_mutex`, but with support for fibers.
 *
 *  This class works in the same way as `std::shared_mutex`, but as it is
 *  implemented in terms of Boost.Fiber primitives, "blocking" operations
 *  are really "yielding" operations.
 *
 *  The class meets the
 *  [SharedMutex](https://en.cppreference.com/w/cpp/named_req/SharedMutex)
 *  requirements.
 */
class shared_mutex
{
public:
    /// Locks the mutex, blocks if the mutex is not available.
    void lock();

    /// Tries to lock the mutex and returns immediately whether it succeeded.
    bool try_lock();

    /// Unlocks the mutex.
    void unlock();

    /**
     *  Locks the mutex for shared ownership, blocks if the mutex is not
     *  available.
     */
    void lock_shared();

    /**
     *  Tries to lock the mutex for shared ownership, returns immediately
     *  whether it succeeded.
     */
    bool try_lock_shared();

    /// Unlocks the mutex from shared ownership.
    void unlock_shared();

private:
    boost::fibers::mutex mutex_;
    boost::fibers::condition_variable condition_;
    int sharedCount_ = 0;
};


/// Whether and how a `file_lock` should acquire a lock on construction.
enum class file_lock_initial_state
{
    /// Do not attempt to acquire a lock, never block.
    not_locked,

    /// Acquire a lock, blocking if necessary.
    locked,

    /// Acquire a shared lock, blocking if necessary.
    locked_shared,
};


/**
 *  A file-based mutual exclusion mechanism.
 *
 *  This class provides interprocess synchronisation based on
 *  `boost::interprocess::file_lock`, augmenting it with support for
 *  inter-fiber and inter-thread synchronisation.  This is achieved by
 *  combining the file lock with a lock on a global `shared_mutex` object
 *  associated with the file.
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
 *  object associated with the same file in each fiber.  If, for some reason,
 *  a `file_lock` *must* be transferred between fibers, do so only when it is
 *  in the unlocked state.
 *
 *  The lock automatically gets unlocked on destruction.
 *
 *  The class meets the
 *  [Lockable](https://en.cppreference.com/w/cpp/named_req/Lockable)
 *  requirements.
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
     *  Two different paths `p1` and `p2` are considered to refer to the
     *  same file if `cosim::filesystem::equivalent(p1,p2)` is `true`.
     *
     *  \param [in] path
     *      The path to the lock file.
     *  \param [in] initialState
     *      Whether and how the lock should be acquired on construction.
     *      May cause the constructor to block until a lock can be acquired.
     *
     *  \throws std::system_error
     *      if the file could not be opened or created.
     */
    explicit file_lock(
        const cosim::filesystem::path& path,
        file_lock_initial_state initialState = file_lock_initial_state::not_locked);

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
    void unlock();

    /**
     *  Acquires a shared lock on the file, blocking if necessary.
     *
     *  \pre
     *      This `file_lock` object is not already locked.
     *      The file is not locked by a different `file_lock` object in the
     *      same fiber.
     */
    void lock_shared();

    /**
     *  Attempts to acquire a shared lock on the file without blocking and
     *  returns whether the attempt was successful.
     *
     *  \pre
     *      This `file_lock` object is not already locked.
     *      The file is not locked by a different `file_lock` object in the
     *      same fiber.
     */
    bool try_lock_shared();

    /**
     *  Unlocks the file from shared ownership.
     *
     *  \pre
     *      This `file_lock` object has been locked for shared ownership in
     *      the current fiber.
     */
    void unlock_shared();

private:
    //  Wraps a boost::interprocess::file_lock and adds two features:
    //    - Creation of file if it doesn't exist
    //    - Shared lock counting (see .cpp file for details)
    //  A bonus is that the interface of this class follows the std conventions
    //  for shared mutexes rather than Boost.Interprocess' slightly different
    //  interface.
    class boost_wrapper
    {
    public:
        explicit boost_wrapper(const cosim::filesystem::path& path);
        void lock();
        bool try_lock();
        void unlock();
        void lock_shared();
        bool try_lock_shared();
        void unlock_shared();

    private:
        boost::interprocess::file_lock fileLock_;
        boost::fibers::mutex shareCountMutex_;
        int shareCount_ = 0; // -1 means exclusive lock
    };

    // Holds the mutex and file lock associated with a particular file.
    struct file_mutex
    {
        file_mutex(const cosim::filesystem::path& path);
        shared_mutex mutex;
        boost_wrapper file;
    };

    // Returns the mutex and file lock associated with the file at `path`.
    static std::shared_ptr<file_mutex> get_file_mutex(
        const cosim::filesystem::path& path);

    // The mutex and file lock associated with this object.
    std::shared_ptr<file_mutex> fileMutex_;

    // The locks we hold on the mutex and the file lock.
    std::variant<std::unique_lock<shared_mutex>, std::shared_lock<shared_mutex>> mutexLock_;
    std::variant<std::unique_lock<boost_wrapper>, std::shared_lock<boost_wrapper>> fileLock_;
};


} // namespace utility
} // namespace cosim
#endif // header guard
