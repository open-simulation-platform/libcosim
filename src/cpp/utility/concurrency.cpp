#if defined(_WIN32) && !defined(NOMINMAX)
#    define NOMINMAX
#endif

#include "cse/utility/concurrency.hpp"

#include "cse/error.hpp"

#include <system_error>
#include <vector>

#ifdef _WIN32
#    include <windows.h>
#else
#    include <cerrno>
#    include <fcntl.h>
#endif


namespace cse
{
namespace utility
{


void shared_mutex::lock()
{
    std::unique_lock<boost::fibers::mutex> lock(mutex_);
    condition_.wait(lock, [&] { return sharedCount_ == 0; });

    // Release the mutex from the unique_lock, so it doesn't get automatically
    // unlocked when the function exits.
    lock.release();
}


bool shared_mutex::try_lock()
{
    std::unique_lock<boost::fibers::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) return false;
    if (sharedCount_ > 0) return false;

    // Release the mutex from the unique_lock, so it doesn't get automatically
    // unlocked when the function exits.
    lock.release();
    return true;
}


void shared_mutex::unlock()
{
    mutex_.unlock();
    condition_.notify_one();
}


void shared_mutex::lock_shared()
{
    std::lock_guard<boost::fibers::mutex> lock(mutex_);
    ++sharedCount_;
}


bool shared_mutex::try_lock_shared()
{
    std::unique_lock<boost::fibers::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock()) return false;
    ++sharedCount_;
    return true;
}


void shared_mutex::unlock_shared()
{
    std::unique_lock<boost::fibers::mutex> lock(mutex_);
    --sharedCount_;
    if (sharedCount_ == 0) {
        lock.unlock();
        condition_.notify_one();
    }
}


namespace
{

boost::interprocess::file_lock make_boost_file_lock(
    const boost::filesystem::path& path)
{
#ifdef _WIN32
    // NOTE: The share mode flags must match those used by boost::interprocess.
    const auto fileHandle = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (fileHandle == INVALID_HANDLE_VALUE) {
        throw std::system_error(
            GetLastError(),
            std::system_category(),
            "Failed to open or create lock file '" + path.string() + "'");
    }
    CloseHandle(fileHandle);
    return boost::interprocess::file_lock(path.string().c_str());
#else
    const auto fileDescriptor = open(path.c_str(), O_CREAT | O_WRONLY, 0777);
    if (fileDescriptor == -1) {
        throw std::system_error(
            errno,
            std::system_category(),
            "Failed to open or create lock file '" + path.string() + "'");
    }
    close(fileDescriptor);
    return boost::interprocess::file_lock(path.c_str());
#endif
}


std::shared_ptr<shared_mutex> get_file_mutex(
    const boost::filesystem::path& path)
{
    struct file_mutex
    {
        boost::filesystem::path path;
        std::weak_ptr<shared_mutex> mutexPtr;
    };
    static std::vector<file_mutex> fileMutexes;
    static boost::fibers::mutex fileMutexesMutex;
    std::shared_ptr<shared_mutex> fileMutex;

    // Do a linear search for an element corresponding to the given path
    // in `fileMutexes`, cleaning up expired elements along the way.
    std::lock_guard<decltype(fileMutexesMutex)> guard(fileMutexesMutex);
    for (auto it = fileMutexes.begin(); it != fileMutexes.end();) {
        if (auto fm = it->mutexPtr.lock()) {
            if (boost::filesystem::equivalent(path, it->path)) {
                assert(!fileMutex);
                fileMutex = std::move(fm);
            }
            ++it;
        } else {
            it = fileMutexes.erase(it);
        }
    }

    // If no element corresponding to the given path was found, create one.
    if (!fileMutex) {
        fileMutex = std::make_shared<shared_mutex>();
        fileMutexes.push_back({boost::filesystem::canonical(path), fileMutex});
    }

    return fileMutex;
}
} // namespace


file_lock::file_lock(
    const boost::filesystem::path& path,
    file_lock_initial_state initialState)
    : fileLock_(make_boost_file_lock(path))
    , mutex_(get_file_mutex(path))
{
    if (initialState == file_lock_initial_state::locked) {
        lock();
    } else if (initialState == file_lock_initial_state::locked_shared) {
        lock_shared();
    }
}


void file_lock::lock()
{
    // NOTE: The reason we can't use std::lock() here is that we must make
    // sure that the mutex gets locked before the file.  Otherwise, the
    // code might block on the file lock, when the lock is in fact held by
    // a different fiber in the same process.  Trying to lock the mutex first
    // gives us a chance to yield to the other fiber if the operation would
    // otherwise block.
    std::unique_lock<shared_mutex> mutexLock(*mutex_);
    fileLock_.lock();
    mutexLock_ = std::move(mutexLock);
}


bool file_lock::try_lock()
{
    // See note on locking order in lock() above.
    std::unique_lock<shared_mutex> mutexLock(*mutex_, std::try_to_lock);
    if (!mutexLock.owns_lock()) return false;
    if (!fileLock_.try_lock()) return false;
    mutexLock_ = std::move(mutexLock);
    return true;
}


void file_lock::unlock()
{
    fileLock_.unlock();
    std::get<std::unique_lock<shared_mutex>>(mutexLock_).unlock();
}


void file_lock::lock_shared()
{
    // See note on locking order in lock() above.
    std::shared_lock<shared_mutex> mutexLock(*mutex_);
    fileLock_.lock_sharable();
    mutexLock_ = std::move(mutexLock);
}


bool file_lock::try_lock_shared()
{
    // See note on locking order in lock() above.
    std::shared_lock<shared_mutex> mutexLock(*mutex_, std::try_to_lock);
    if (!mutexLock.owns_lock()) return false;
    if (!fileLock_.try_lock_sharable()) return false;
    mutexLock_ = std::move(mutexLock);
    return true;
}


void file_lock::unlock_shared()
{
    fileLock_.unlock_sharable();
    std::get<std::shared_lock<shared_mutex>>(mutexLock_).unlock();
}


} // namespace utility
} // namespace cse
