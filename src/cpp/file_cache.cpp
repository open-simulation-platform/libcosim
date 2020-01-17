#include "cse/file_cache.hpp"

#include "cse/log/logger.hpp"
#include "cse/uri.hpp"
#include "cse/utility/concurrency.hpp"
#include "cse/utility/filesystem.hpp"

#include <stdexcept>
#include <string>
#include <unordered_map>


namespace cse
{


// =============================================================================
// temporary_file_cache
// =============================================================================

namespace
{

// In the following, we'll use std::shared_ptr and std::weak_ptr as an easy
// way of tracking the lifetime of a `temporary_file_cache_directory`
// object.  In short, once such an object expires, the reference count for
// its `ownership_` shared pointer gets decremented, and when the reference
// count reaches zero, the corresponding `weak_ptr` in
// `temporary_file_cache::impl::ownerships_` expires.  Then we know that access
// to the directory has been relinquished by all.
//
// Since we don't care about the actual contents of the pointed-to object, we
// define a dummy object type.

struct dummy
{
};

class temporary_file_cache_directory
    : public file_cache::directory_rw,
      public file_cache::directory_ro
{
public:
    temporary_file_cache_directory(
        std::shared_ptr<utility::temp_dir> cache,
        const boost::filesystem::path& path,
        std::shared_ptr<dummy> ownership)
        : cache_(cache)
        , path_(path)
        , ownership_(ownership)
    {
    }

    boost::filesystem::path path() const override
    {
        return path_;
    }

private:
    std::shared_ptr<utility::temp_dir> cache_;
    boost::filesystem::path path_;
    std::shared_ptr<dummy> ownership_;
};

} // namespace


class temporary_file_cache::impl
{
public:
    impl()
        : root_(std::make_shared<utility::temp_dir>())
    {}

    std::unique_ptr<directory_rw> get_directory_rw(std::string_view key)
    {
        auto& owns = get_ownership(std::string(key), true);
        if (owns.rw.use_count() + owns.ro.use_count() > 0) {
            throw std::logic_error(
                "Cache subdirectory already in use: " + std::string(key));
        }
        const auto path = root_->path() / percent_encode(key);
        boost::filesystem::create_directories(path);

        auto ownership = std::make_shared<dummy>();
        owns.rw = ownership;
        return std::make_unique<temporary_file_cache_directory>(root_, path, ownership);
    }

    std::unique_ptr<directory_ro> get_directory_ro(std::string_view key)
    {
        auto& owns = get_ownership(std::string(key), false);
        if (owns.rw.use_count() > 0) {
            throw std::logic_error(
                "Cache subdirectory already in use: " + std::string(key));
        }
        const auto path = root_->path() / percent_encode(key);

        auto ownership = std::make_shared<dummy>();
        owns.ro = ownership;
        return std::make_unique<temporary_file_cache_directory>(root_, path, ownership);
    }

private:
    struct subdirectory_ownership
    {
        std::weak_ptr<dummy> rw;
        std::weak_ptr<dummy> ro;
    };

    subdirectory_ownership& get_ownership(const std::string& key, bool create)
    {
        auto it = ownerships_.find(key);
        if (it == ownerships_.end()) {
            if (create) {
                it = ownerships_.emplace(key, subdirectory_ownership{}).first;
            } else {
                throw std::logic_error("Invalid cache subdirectory key: " + key);
            }
        }
        return it->second;
    }

    std::shared_ptr<utility::temp_dir> root_;
    std::unordered_map<std::string, subdirectory_ownership> ownerships_;
};


temporary_file_cache::temporary_file_cache()
    : impl_(std::make_unique<impl>())
{
}


temporary_file_cache::temporary_file_cache(temporary_file_cache&&) noexcept = default;
temporary_file_cache& temporary_file_cache::operator=(temporary_file_cache&&) noexcept = default;
temporary_file_cache::~temporary_file_cache() noexcept = default;


std::unique_ptr<file_cache::directory_rw> temporary_file_cache::get_directory_rw(std::string_view key)
{
    return impl_->get_directory_rw(key);
}


std::unique_ptr<file_cache::directory_ro> temporary_file_cache::get_directory_ro(std::string_view key)
{
    return impl_->get_directory_ro(key);
}


// =============================================================================
// persistent_file_cache
// =============================================================================

class persistent_file_cache_directory
    : public file_cache::directory_rw,
      public file_cache::directory_ro
{
public:
    persistent_file_cache_directory(
        const boost::filesystem::path& path,
        utility::file_lock lock)
        : path_(path)
        , lock_(std::move(lock))
    {
    }

    boost::filesystem::path path() const override
    {
        return path_;
    }

private:
    boost::filesystem::path path_;
    utility::file_lock lock_;
};


class persistent_file_cache::impl
{
public:
    explicit impl(const boost::filesystem::path& root)
        : root_(root)
    {
        boost::filesystem::create_directories(root_);
    }

    std::unique_ptr<directory_rw> get_directory_rw(std::string_view key)
    {
        auto rootLock = utility::file_lock(
            root_lock_file_path(),
            utility::file_lock_initial_state::locked);

        const auto path = subdir_path(key);
        auto lock = utility::file_lock(
            subdir_lock_file_path(key),
            utility::file_lock_initial_state::locked);

        boost::filesystem::create_directories(path);
        return std::make_unique<persistent_file_cache_directory>(path, std::move(lock));
    }

    std::unique_ptr<directory_ro> get_directory_ro(std::string_view key)
    {
        auto rootLock = utility::file_lock(
            root_lock_file_path(),
            utility::file_lock_initial_state::locked_shared);

        const auto path = subdir_path(key);
        auto lock = utility::file_lock(
            subdir_lock_file_path(key),
            utility::file_lock_initial_state::locked_shared);

        if (boost::filesystem::exists(path)) {
            return std::make_unique<persistent_file_cache_directory>(path, std::move(lock));
        } else {
            throw std::logic_error(
                "Invalid cache subdirectory key: " + std::string(key));
        }
    }

    void cleanup()
    {
        auto rootLock = utility::file_lock(
            root_lock_file_path(),
            utility::file_lock_initial_state::locked);

        for (auto it = boost::filesystem::directory_iterator(root_);
             it != boost::filesystem::directory_iterator();
             ++it) {
            if (it->status().type() == boost::filesystem::directory_file &&
                it->path().extension() == ".data") {

                // Remove non-locked subdirectories and their lock files
                auto subdirLockFilePath = it->path();
                subdirLockFilePath.replace_extension(".lock");
                auto subdirLock = utility::file_lock(
                    subdirLockFilePath,
                    utility::file_lock_initial_state::not_locked);
                if (!subdirLock.try_lock()) continue;

                boost::system::error_code ignoredError;
                boost::filesystem::remove_all(*it, ignoredError);
                boost::filesystem::remove(subdirLockFilePath);
            } else if (it->status().type() == boost::filesystem::regular_file &&
                it->path().extension() == ".lock") {

                // Remove lock files left over by get_directory_ro() for
                // nonexistent subdirectories.
                auto subdirLock = utility::file_lock(
                    it->path(),
                    utility::file_lock_initial_state::not_locked);
                if (!subdirLock.try_lock()) continue;

                auto subdirPath = it->path();
                subdirPath.replace_extension(".data");
                if (!boost::filesystem::exists(subdirPath)) {
                    boost::system::error_code ignoredError;
                    boost::filesystem::remove(*it, ignoredError);
                }
            }
        }
    }

private:
    boost::filesystem::path root_lock_file_path() const
    {
        return root_ / "lock";
    }

    boost::filesystem::path subdir_path(std::string_view key) const
    {
        auto p = root_;
        p /= percent_encode(key);
        p += ".data";
        return p;
    }

    boost::filesystem::path subdir_lock_file_path(std::string_view key) const
    {
        auto p = root_;
        p /= percent_encode(key);
        p += ".lock";
        return p;
    }

    boost::filesystem::path root_;
};


persistent_file_cache::persistent_file_cache(
    const boost::filesystem::path& cacheRoot)
    : impl_(std::make_unique<impl>(cacheRoot))
{
}


persistent_file_cache::persistent_file_cache(persistent_file_cache&&) noexcept = default;
persistent_file_cache& persistent_file_cache::operator=(persistent_file_cache&&) noexcept = default;
persistent_file_cache::~persistent_file_cache() noexcept = default;


std::unique_ptr<file_cache::directory_rw> persistent_file_cache::get_directory_rw(std::string_view key)
{
    return impl_->get_directory_rw(key);
}


std::unique_ptr<file_cache::directory_ro> persistent_file_cache::get_directory_ro(std::string_view key)
{
    return impl_->get_directory_ro(key);
}


void persistent_file_cache::cleanup()
{
    impl_->cleanup();
}


} // namespace cse
