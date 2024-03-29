/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/utility/concurrency.hpp"

#include <cassert>
#include <system_error>
#include <vector>

#ifdef _WIN32
#    include <windows.h>
#else
#    include <cerrno>
#    include <fcntl.h>
#endif


namespace cosim
{
namespace utility
{

// =============================================================================
// file_lock
// =============================================================================


file_lock::file_lock(
    const cosim::filesystem::path& path,
    file_lock_initial_state initialState)
    : fileMutex_(get_file_mutex(path))
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
    // sure that the mutex gets locked before the file, since
    // boost::interprocess::file_lock isn't thread safe.
    std::unique_lock<std::shared_mutex> mutexLock(fileMutex_->mutex);
    std::unique_lock<boost_wrapper> fileLock(fileMutex_->file);
    mutexLock_ = std::move(mutexLock);
    fileLock_ = std::move(fileLock);
}


bool file_lock::try_lock()
{
    // See note on locking order in lock() above.
    std::unique_lock<std::shared_mutex> mutexLock(fileMutex_->mutex, std::try_to_lock);
    if (!mutexLock.owns_lock()) return false;
    std::unique_lock<boost_wrapper> fileLock(fileMutex_->file, std::try_to_lock);
    if (!fileLock.owns_lock()) return false;
    mutexLock_ = std::move(mutexLock);
    fileLock_ = std::move(fileLock);
    return true;
}


void file_lock::unlock()
{
    std::get<std::unique_lock<boost_wrapper>>(fileLock_).unlock();
    std::get<std::unique_lock<std::shared_mutex>>(mutexLock_).unlock();
}


void file_lock::lock_shared()
{
    // See note on locking order in lock() above.
    std::shared_lock<std::shared_mutex> mutexLock(fileMutex_->mutex);
    std::shared_lock<boost_wrapper> fileLock(fileMutex_->file);
    mutexLock_ = std::move(mutexLock);
    fileLock_ = std::move(fileLock);
}


bool file_lock::try_lock_shared()
{
    // See note on locking order in lock() above.
    std::shared_lock<std::shared_mutex> mutexLock(fileMutex_->mutex, std::try_to_lock);
    if (!mutexLock.owns_lock()) return false;
    std::shared_lock<boost_wrapper> fileLock(fileMutex_->file, std::try_to_lock);
    if (!fileLock.owns_lock()) return false;
    mutexLock_ = std::move(mutexLock);
    fileLock_ = std::move(fileLock);
    return true;
}


void file_lock::unlock_shared()
{
    std::get<std::shared_lock<boost_wrapper>>(fileLock_).unlock();
    std::get<std::shared_lock<std::shared_mutex>>(mutexLock_).unlock();
}


file_lock::boost_wrapper::boost_wrapper(const cosim::filesystem::path& path)
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
    fileLock_ = boost::interprocess::file_lock(path.string().c_str());
#else
    const auto fileDescriptor = open(path.c_str(), O_CREAT | O_WRONLY, 0666);
    if (fileDescriptor == -1) {
        throw std::system_error(
            errno,
            std::system_category(),
            "Failed to open or create lock file '" + path.string() + "'");
    }
    close(fileDescriptor);
    fileLock_ = boost::interprocess::file_lock(path.c_str());
#endif
}


/*  A note on the shared lock counting in file_lock::boost_wrapper:
 *
 *  This is needed to be able to share a boost::interprocess::file_lock object
 *  between several threads.  It enables this scenario, where f is a
 *  boost_wrapper:
 *
 *   1. Thread A calls f.lock_shared(), lock is acquired
 *   2. Thread B calls f.lock_shared()
 *   3. Thread A calls f.unlock_shared()
 *   4. Thread B calls f.unlock_shared(), lock is released
 *
 *  The order in which the threads unlock f does not matter; the lock is always
 *  released on the last one.
 *
 *  If f were a plain boost::interprocess::file_lock, the lock would be released
 *  the *first* time a thread calls f.unlock_shared(), since it is just a thin
 *  wrapper around the OS file locking facilities, and file locks are generally
 *  process-wide.
 *
 *  Note also that boost_wrapper does not deal with *exclusive* locks at all.
 *  This is handled at a higher level by the shared_mutex in
 *  cosim::utility::file_lock, which prevents several threads from attempting to
 *  acquire exclusive file locks at once, since they have to get the mutex
 *  first.  (See file_lock::lock().)
 */

void file_lock::boost_wrapper::lock()
{
    std::lock_guard<std::mutex> countLock(shareCountMutex_);
    assert(shareCount_ == 0);
    fileLock_.lock();
    shareCount_ = -1;
}


bool file_lock::boost_wrapper::try_lock()
{
    std::lock_guard<std::mutex> countLock(shareCountMutex_);
    assert(shareCount_ == 0);
    if (!fileLock_.try_lock()) return false;
    shareCount_ = -1;
    return true;
}


void file_lock::boost_wrapper::unlock()
{
    std::lock_guard<std::mutex> countLock(shareCountMutex_);
    assert(shareCount_ == -1);
    fileLock_.unlock();
    shareCount_ = 0;
}


void file_lock::boost_wrapper::lock_shared()
{
    std::lock_guard<std::mutex> countLock(shareCountMutex_);
    assert(shareCount_ >= 0);
    if (shareCount_ == 0) {
        fileLock_.lock_sharable();
    }
    ++shareCount_;
}


bool file_lock::boost_wrapper::try_lock_shared()
{
    std::lock_guard<std::mutex> countLock(shareCountMutex_);
    assert(shareCount_ >= 0);
    if (shareCount_ == 0) {
        if (!fileLock_.try_lock_sharable()) return false;
    }
    ++shareCount_;
    return true;
}


void file_lock::boost_wrapper::unlock_shared()
{
    std::lock_guard<std::mutex> countLock(shareCountMutex_);
    assert(shareCount_ > 0);
    if (shareCount_ == 1) fileLock_.unlock_sharable();
    --shareCount_;
}


file_lock::file_mutex::file_mutex(const cosim::filesystem::path& path)
    : file(path)
{ }


std::shared_ptr<file_lock::file_mutex> file_lock::get_file_mutex(
    const cosim::filesystem::path& path)
{
    struct file_mutex_cache_entry
    {
        cosim::filesystem::path path;
        std::weak_ptr<file_mutex> fileMutex;
    };
    static std::vector<file_mutex_cache_entry> fileMutexCache;
    static std::mutex cacheMutex;
    std::shared_ptr<file_mutex> fileMutex;

    // If the file already exists, we need to check whether it is already
    // associated with a mutex.  We do so by performing a linear search for
    // an element corresponding to the given path in `fileMutexes`
    // (cleaning up any expired elements along the way).
    std::lock_guard<decltype(cacheMutex)> guard(cacheMutex);
    if (cosim::filesystem::exists(path)) {
        for (auto it = fileMutexCache.begin(); it != fileMutexCache.end();) {
            if (auto fm = it->fileMutex.lock()) {
                if (cosim::filesystem::equivalent(path, it->path)) {
                    assert(!fileMutex);
                    fileMutex = std::move(fm);
                }
                ++it;
            } else {
                it = fileMutexCache.erase(it);
            }
        }
    }

    // If no element corresponding to the given path was found, create one.
    if (!fileMutex) {
        fileMutex = std::make_shared<file_mutex>(path);
        fileMutexCache.push_back({cosim::filesystem::canonical(path), fileMutex});
    }

    return fileMutex;
}


} // namespace utility
} // namespace cosim
