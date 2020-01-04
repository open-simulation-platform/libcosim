#ifdef _WIN32
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
    const auto fileHandle = CreateFileW(
        path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_WRITE,
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
    std::lock(fileLock_, mutexLock_);
}


bool file_lock::try_lock()
{
    return std::try_lock(fileLock_, mutexLock_) == -1;
}


void file_lock::unlock() noexcept
{
    fileLock_.unlock();
    mutexLock_.unlock();
}


} // namespace utility
} // namespace cse
