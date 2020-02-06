#ifndef CSE_FILE_CACHE_HPP
#define CSE_FILE_CACHE_HPP

#include <boost/filesystem.hpp>

#include <memory>
#include <string_view>


namespace cse
{


/**
 *  An interface to a file cache.
 *
 *  Conceptually, the cache is organised as a flat list of subdirectories,
 *  each associated with a name – here called a *key*, so as not to confuse
 *  it with the actual directory name, which may be different.
 *  (The actual filesystem layout of the cache is implementation specific.)
 *
 *  Client code can request read-write or read-only access to specific
 *  subdirectories.  Since it may be impossible for the implementation to
 *  restrict access on a filesystem level, it is up to client code to not
 *  abuse the API by modifying the contents of a subdirectory after
 *  requesting read-only access.
 */
class file_cache
{
public:
    /// A handle that represents read/write access to a cache subdirectory.
    class directory_rw
    {
    public:
        /// The filesystem path to the subdirectory.
        virtual boost::filesystem::path path() const = 0;
        virtual ~directory_rw() = default;
    };

    /// A handle that represents read-only access to a cache subdirectory.
    class directory_ro
    {
    public:
        /// The filesystem path to the subdirectory.
        virtual boost::filesystem::path path() const = 0;
        virtual ~directory_ro() = default;
    };

    /**
     *  Requests read/write access to the cache subdirectory associated with
     *  the given key, creating one if it doesn't exist already.
     *
     *  Access is granted for the lifetime of the returned `directory_rw` object.
     *  The object should therefore be kept around as long as access is needed,
     *  but usually no longer, since it may block others from gaining access.
     *  Once the object expires, the directory may be modified or deleted by
     *  others.
     *
     *  If the function is unable to take ownership of the directory,
     *  it may block until it becomes able to do so, or it may throw.
     */
    virtual std::unique_ptr<directory_rw> get_directory_rw(std::string_view key) = 0;

    /**
     *  Requests read-only access to the cache subdirectory associated with
     *  the given key.  The key must already exist in the cache.
     *
     *  Access is granted for the lifetime of the returned `directory_ro` object.
     *  The object should therefore be kept around as long as access is needed,
     *  but usually no longer, since it may block others from gaining access.
     *  Once the object expires, the directory may be modified or deleted by
     *  others.
     *
     *  If the function is unable to take shared ownership of the directory,
     *  it may block until it becomes able to do so, or it may throw.
     */
    virtual std::unique_ptr<directory_ro> get_directory_ro(std::string_view key) = 0;

    virtual ~file_cache() = default;
};


/**
 *  A simple implementation of `file_cache` that offers no synchronisation
 *  or persistence.
 *
 *  Upon construction, a new cache will be created in a randomly-named
 *  temporary location, and it will be removed again on destruction.
 *  The class may only be safely used by one thread at a time.
 */
class temporary_file_cache : public file_cache
{
public:
    temporary_file_cache();

    temporary_file_cache(const temporary_file_cache&) = delete;
    temporary_file_cache& operator=(const temporary_file_cache&) = delete;
    temporary_file_cache(temporary_file_cache&&) noexcept;
    temporary_file_cache& operator=(temporary_file_cache&&) noexcept;
    ~temporary_file_cache() noexcept;

    std::unique_ptr<directory_rw> get_directory_rw(std::string_view key) override;
    std::unique_ptr<directory_ro> get_directory_ro(std::string_view key) override;

private:
    class impl;
    std::unique_ptr<impl> impl_;
};


/**
 *  A persistent file cache which can be safely accessed by multiple
 *  processes, threads and fibers concurrently.
 */
class persistent_file_cache : public file_cache
{
public:
    /**
     *  Uses `cacheRoot` as the top-level directory of the cache.
     *
     *  It is recommended that this directory be managed in its entirety by
     *  `persistent_file_cache`, i.e., that no other files are stored in it.
     */
    explicit persistent_file_cache(const boost::filesystem::path& cacheRoot);

    persistent_file_cache(const persistent_file_cache&) = delete;
    persistent_file_cache& operator=(const persistent_file_cache&) = delete;
    persistent_file_cache(persistent_file_cache&&) noexcept;
    persistent_file_cache& operator=(persistent_file_cache&&) noexcept;
    ~persistent_file_cache() noexcept;

    std::unique_ptr<directory_rw> get_directory_rw(std::string_view key) override;
    std::unique_ptr<directory_ro> get_directory_ro(std::string_view key) override;

    /**
     *  Cleans up cache contents.
     *
     *  This will delete all subdirectories that are not currently being
     *  used (i.e., for which there exist `directory_r[ow]` handles).
     */
    void cleanup();

private:
    class impl;
    std::unique_ptr<impl> impl_;
};


} // namespace cse
#endif
