#if defined(_WIN32) && !defined(NOMINMAX)
#    define NOMINMAX
#endif
#include "cse/utility/filesystem.hpp"

#include "cse/utility/uuid.hpp"

#include <system_error>
#include <utility>

#ifdef _WIN32
#    include <cstring>
#    include <windows.h>
#else
#    include <fcntl.h>
#    include <sys/file.h>
#endif


namespace cse
{
namespace utility
{


temp_dir::temp_dir(const boost::filesystem::path& parent)
{
    if (parent.empty()) {
        path_ = boost::filesystem::temp_directory_path() / ("cse_" + random_uuid());
    } else if (parent.is_absolute()) {
        path_ = parent / random_uuid();
    } else {
        path_ = boost::filesystem::temp_directory_path() / parent / random_uuid();
    }
    boost::filesystem::create_directories(path_);
}

temp_dir::temp_dir(temp_dir&& other) noexcept
    : path_(std::move(other.path_))
{
    other.path_.clear();
}

temp_dir& temp_dir::operator=(temp_dir&& other) noexcept
{
    delete_noexcept();
    path_ = std::move(other.path_);
    other.path_.clear();
    return *this;
}

temp_dir::~temp_dir()
{
    delete_noexcept();
}

const boost::filesystem::path& temp_dir::path() const
{
    return path_;
}

void temp_dir::delete_noexcept() noexcept
{
    if (!path_.empty()) {
        boost::system::error_code errorCode;
        boost::filesystem::remove_all(path_, errorCode);
        path_.clear();
    }
}



lock_file::lock_file(const boost::filesystem::path& path)
    : path_(path)
{
#ifdef _WIN32
    file_ = CreateFileW(
        path_.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_WRITE,
        NULL,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (file_ == INVALID_HANDLE_VALUE) {
        throw std::system_error(
            GetLastError(),
            std::system_category(),
            "Failed to create lock file '" + path_.string() + "'");
    }
#else
    file_ = open(path_.c_str(), O_CREAT | O_WRONLY, 0777);
    if (file_ == -1) {
        throw std::system_error(
            errno,
            std::system_category(),
            "Failed to create lock file '" + path_.string() + "'");
    }
#endif
}


namespace
{
#ifdef _WIN32

bool os_lock_file(
    HANDLE file,
    const boost::filesystem::path& path,
    bool block)
{
    const auto flags = block
        ? LOCKFILE_EXCLUSIVE_LOCK
        : (LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY);
    OVERLAPPED overlapped;
    std::memset(&overlapped, 0, sizeof overlapped);
    const auto locked = LockFileEx(file, flags, 0, 1, 0, &overlapped);
    if (!locked) {
        // TODO: Check error code for non-blocking lock failure. ERROR_CANT_WAIT?
        if (!block) return false;
        throw std::system_error(
            GetLastError(),
            std::system_category(),
            "Failed to lock file '" + path.string() + "' for exclusive access");
    }
    return true;
}

void os_unlock_file(HANDLE file) noexcept
{
    UnlockFile(file, 0, 0, 1, 0);
}

#else

bool os_lock_file(
    int file,
    const boost::filesystem::path& path,
    bool block)
{
    const auto operation = block ? LOCK_EX : (LOCK_EX | LOCK_NB);
    const auto locked = flock(file, operation);
    if (locked == -1) {
        if (!block && errno == EWOULDBLOCK) return false;
        throw std::system_error(
            errno,
            std::system_category(),
            "Failed to lock file '" + path.string() + "' for exclusive access");
    }
    return true;
}

void os_unlock_file(int file) noexcept
{
    flock(file, LOCK_UN);
}

#endif
} // namespace


void lock_file::lock()
{
    os_lock_file(file_, path_, true);
}


bool lock_file::try_lock()
{
    return os_lock_file(file_, path_, false);
}


void lock_file::unlock() noexcept
{
    os_unlock_file(file_);
}


lock_file::~lock_file() noexcept
{
    unlock();
#ifdef _WIN32
    CloseHandle(file_);
#else
    close(file_);
#endif
}


} // namespace utility
} // namespace cse
