#include "cse/utility/filesystem.hpp"

#include <system_error>
#include <utility>

#include "cse/utility/uuid.hpp"


namespace cse
{
namespace utility
{


temp_dir::temp_dir(const std::filesystem::path& parent)
{
    if (parent.empty()) {
        path_ = std::filesystem::temp_directory_path() / random_uuid();
    } else if (parent.is_absolute()) {
        path_ = parent / random_uuid();
    } else {
        path_ = std::filesystem::temp_directory_path() / parent / random_uuid();
    }
    std::filesystem::create_directories(path_);
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

const std::filesystem::path& temp_dir::path() const
{
    return path_;
}

void temp_dir::delete_noexcept() noexcept
{
    if (!path_.empty()) {
        std::error_code ignoredError;
        std::filesystem::remove_all(path_, ignoredError);
        path_.clear();
    }
}


} // namespace utility
} // namespace cse
