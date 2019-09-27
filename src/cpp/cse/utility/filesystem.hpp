/**
 *  \file
 *  File system utilities.
 */
#ifndef CSE_UTILITY_FILESYSTEM_HPP
#define CSE_UTILITY_FILESYSTEM_HPP

#include <boost/filesystem.hpp>


namespace cse
{
namespace utility
{


/**
 *  An RAII object that creates a unique directory on construction and
 *  recursively deletes it again on destruction.
*/
class temp_dir
{
public:
    /**
     *  Creates a new temporary directory.
     *
     *  The name of the new directory will be randomly generated, and there are
     *  three options of where it will be created, depending on the value of
     *  `parent`.  In the following, `temp` refers to a directory suitable for
     *  temporary files under the conventions of the operating system (e.g.
     *  `/tmp` under UNIX-like systems), and `name` refers to the randomly
     *  generated name mentioned above.
     *
     *    - If `parent` is empty: `temp/name`
     *    - If `parent` is relative: `temp/parent/name`
     *    - If `parent` is absolute: `parent/name`
    */
    explicit temp_dir(
        const boost::filesystem::path& parent = boost::filesystem::path());

    temp_dir(const temp_dir&) = delete;
    temp_dir& operator=(const temp_dir&) = delete;

    /**
     *  Move constructor.
     *
     *  Ownership of the directory is transferred from `other` to `this`.
     *  Afterwards, `other` no longer refers to any directory, meaning that
     *  `other.Path()` will return an empty path, and its destructor will not
     *  perform any filesystem operations.
    */
    temp_dir(temp_dir&& other) noexcept;

    /// Move assignment operator. See temp_dir(temp_dir&&) for semantics.
    temp_dir& operator=(temp_dir&&) noexcept;

    /// Destructor.  Recursively deletes the directory.
    ~temp_dir() noexcept;

    /// Returns the path to the directory.
    const boost::filesystem::path& path() const;

private:
    void delete_noexcept() noexcept;

    boost::filesystem::path path_;
};


/**
 *  Manages a lock file for interprocess synchronisation.
 *
 *  This class meets the
 *  [Lockable](https://en.cppreference.com/w/cpp/named_req/Lockable)
 *  requirements.
 */
class lock_file
{
public:
    /**
     *  Constructs an object that uses the file at `path` as a lock file.
     *
     *  The file must be writable by the current process, but it will not be
     *  modified if it already exists.  If it does not exist, it will be
     *  created.
     */
    explicit lock_file(const boost::filesystem::path& path);

    /// Acquires a lock on the file, blocking if necessary.
    void lock();

    /**
     *  Attempts to acquire a lock on the file without blocking and returns
     *  whether the attempt was successful.
     */
    bool try_lock();

    /// Releases the lock, if one is held.
    void unlock() noexcept;

    /**
     *  Releases the lock, if one is held.
     *
     *  The file gets closed but not deleted.
     */
    ~lock_file() noexcept;

private:
    boost::filesystem::path path_;
#ifdef _WIN32
    void* file_;
#else
    int file_;
#endif
};


} // namespace utility
} // namespace cse
#endif // header guard
