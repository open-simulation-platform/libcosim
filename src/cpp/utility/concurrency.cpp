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


std::shared_ptr<boost::fibers::mutex> get_file_mutex(
    const boost::filesystem::path& path)
{
    struct file_mutex
    {
        boost::filesystem::path path;
        std::weak_ptr<boost::fibers::mutex> mutexPtr;
    };
    static std::vector<file_mutex> fileMutexes;
    static boost::fibers::mutex fileMutexesMutex;
    std::shared_ptr<boost::fibers::mutex> fileMutex;

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
        fileMutex = std::make_shared<boost::fibers::mutex>();
        fileMutexes.push_back({boost::filesystem::canonical(path), fileMutex});
    }

    return fileMutex;
}

} // namespace


file_lock::file_lock(const boost::filesystem::path& path)
    : fileLock_(make_boost_file_lock(path))
    , mutex_(get_file_mutex(path))
    , mutexLock_(*mutex_, std::defer_lock)
{
}


void file_lock::lock()
{
    // NOTE: The reason we can't use std::lock() here is that we must make
    // sure that the mutex gets locked before the file.  Otherwise, the
    // code might block on the file lock, when the lock is in fact held by
    // a different fiber in the same process.  Trying to lock the mutex first
    // gives us a chance to yield to the other fiber if the operation would
    // otherwise block.
    mutexLock_.lock();
    try {
        fileLock_.lock();
    } catch (...) {
        mutexLock_.unlock();
        throw;
    }
}


bool file_lock::try_lock()
{
    // We try the locks in the same order as for lock().
    if (!mutexLock_.try_lock()) return false;
    try {
        if (!fileLock_.try_lock()) {
            mutexLock_.unlock();
            return false;
        }
        return true;
    } catch (...) {
        mutexLock_.unlock();
        throw;
    }
}


void file_lock::unlock() noexcept
{
    fileLock_.unlock();
    mutexLock_.unlock();
}


} // namespace utility
} // namespace cse
