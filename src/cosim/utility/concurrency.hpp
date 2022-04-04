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

#include <boost/interprocess/sync/file_lock.hpp>

#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <variant>


namespace cosim
{
namespace utility
{

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
 *  inter-thread synchronisation.  This is achieved by combining the file lock
 *  with a lock on a global `std::shared_mutex` object associated with the
 *  file.
 *
 *  Note that `file_lock` objects should not be shared among threads. The
 *  inter-thread synchronisation is handled internally via the global
 *  per-file mutex.
 *
 *  Furthermore, once a thread has locked a file, the same thread may not
 *  attempt to use a different `file_lock` object to lock the same file, as
 *  this would cause a deadlock.  (This is also because they would share the
 *  same global mutex.)
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
     *      same thread.
     */
    void lock();

    /**
     *  Attempts to acquire a lock on the file without blocking and returns
     *  whether the attempt was successful.
     *
     *  \pre
     *      This `file_lock` object is not already locked.
     *      The file is not locked by a different `file_lock` object in the
     *      same thread.
     */
    bool try_lock();

    /**
     *  Unlocks the file.
     *
     *  \pre
     *      This `file_lock` object has been locked in the current thread.
     */
    void unlock();

    /**
     *  Acquires a shared lock on the file, blocking if necessary.
     *
     *  \pre
     *      This `file_lock` object is not already locked.
     *      The file is not locked by a different `file_lock` object in the
     *      same thread.
     */
    void lock_shared();

    /**
     *  Attempts to acquire a shared lock on the file without blocking and
     *  returns whether the attempt was successful.
     *
     *  \pre
     *      This `file_lock` object is not already locked.
     *      The file is not locked by a different `file_lock` object in the
     *      same thread.
     */
    bool try_lock_shared();

    /**
     *  Unlocks the file from shared ownership.
     *
     *  \pre
     *      This `file_lock` object has been locked for shared ownership in
     *      the current thread.
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
        std::mutex shareCountMutex_;
        int shareCount_ = 0; // -1 means exclusive lock
    };

    // Holds the mutex and file lock associated with a particular file.
    struct file_mutex
    {
        file_mutex(const cosim::filesystem::path& path);
        std::shared_mutex mutex;
        boost_wrapper file;
    };

    // Returns the mutex and file lock associated with the file at `path`.
    static std::shared_ptr<file_mutex> get_file_mutex(
        const cosim::filesystem::path& path);

    // The mutex and file lock associated with this object.
    std::shared_ptr<file_mutex> fileMutex_;

    // The locks we hold on the mutex and the file lock.
    std::variant<std::unique_lock<std::shared_mutex>, std::shared_lock<std::shared_mutex>> mutexLock_;
    std::variant<std::unique_lock<boost_wrapper>, std::shared_lock<boost_wrapper>> fileLock_;
};


} // namespace utility
} // namespace cosim
#endif // header guard
