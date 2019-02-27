#include "cse/utility/filesystem.hpp"

#include "cse/utility/uuid.hpp"

#include <system_error>
#include <utility>


namespace cse
{
namespace utility
{


temp_dir::temp_dir(const boost::filesystem::path& parent)
{
    if (parent.empty()) {
        path_ = boost::filesystem::temp_directory_path() / ("cse_"  + random_uuid());
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


} // namespace utility
} // namespace cse
