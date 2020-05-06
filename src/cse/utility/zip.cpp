#include "zip.hpp"

#include <zip.h>

#include <cassert>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <system_error>
#include <vector>


namespace cosim
{
namespace utility
{
namespace zip
{


namespace
{
// A simple RAII class that manages a zip_file*.
class auto_zip_file
{
public:
    auto_zip_file(::zip* archive, zip_uint64_t index, zip_flags_t flags = 0)
        : m_file{zip_fopen_index(archive, index, flags)}
    {
        if (m_file == nullptr) {
            throw error(archive);
        }
    }

    // Disabled because we don't need them (for now):
    auto_zip_file(const auto_zip_file&) = delete;
    auto_zip_file& operator=(const auto_zip_file&) = delete;
    auto_zip_file(auto_zip_file&&) = delete;
    auto_zip_file& operator=(auto_zip_file&&) = delete;

    ~auto_zip_file() noexcept
    {
        if (m_file) zip_fclose(m_file);
    }

    std::size_t read(void* buffer, std::size_t maxBytes)
    {
        assert(m_file != nullptr);
        assert(buffer != nullptr);
        assert(maxBytes > 0);
        const auto bytesRead = zip_fread(m_file, buffer, maxBytes);
        if (bytesRead < 0) {
            throw error(m_file);
        }
        return static_cast<std::size_t>(bytesRead);
    }

private:
    zip_file* m_file;
};
} // namespace


archive::archive() noexcept
    : m_archive{nullptr}
{
}


archive::archive(const boost::filesystem::path& path)
    : m_archive{nullptr}
{
    open(path);
}


archive::archive(archive&& other) noexcept
    : m_archive{other.m_archive}
{
    other.m_archive = nullptr;
}


archive& archive::operator=(archive&& other) noexcept
{
    discard();
    m_archive = other.m_archive;
    other.m_archive = nullptr;
    return *this;
}


archive::~archive() noexcept
{
    discard();
}


void archive::open(const boost::filesystem::path& path)
{
    assert(!is_open());
    int errorCode;
    auto archive = zip_open(path.string().c_str(), 0, &errorCode);
    if (!archive) {
        const auto errnoVal = (errorCode == ZIP_ER_READ ? errno : 0);
        auto msgBuf = std::vector<char>(
            zip_error_to_str(nullptr, 0, errorCode, errnoVal) + 1);
        zip_error_to_str(msgBuf.data(), msgBuf.size(), errorCode, errno);
        throw error("Unzipping of file: '" + path.string() + "' failed with error: " + msgBuf.data());
    }
    m_archive = archive;
}


/*
The reason this function is not called "Close" is that libzip has a separate
zip_close() function which saves changes to the archive, whereas zip_discard()
does not save changes.  Since this module currently only supports non-modifying
operations, there would be no practical difference, but this way we keep the
door open for adding this functionality in the future.
*/
void archive::discard() noexcept
{
    if (m_archive) {
        zip_discard(m_archive);
        m_archive = nullptr;
    }
}


bool archive::is_open() const noexcept
{
    return m_archive != nullptr;
}


std::uint64_t archive::entry_count() const
{
    assert(is_open());
    return zip_get_num_entries(m_archive, 0);
}


entry_index archive::find_entry(const std::string& name) const
{
    assert(is_open());
    const auto n = zip_name_locate(m_archive, name.c_str(), ZIP_FL_ENC_GUESS);
    if (n < 0) {
        int code = 0;
        zip_error_get(m_archive, &code, nullptr);
        if (code == ZIP_ER_NOENT)
            return invalid_entry_index;
        else
            throw error(m_archive);
    }
    return static_cast<entry_index>(n);
}


std::string archive::entry_name(entry_index index) const
{
    assert(is_open());
    const auto name = zip_get_name(m_archive, index, ZIP_FL_ENC_GUESS);
    if (name == nullptr) {
        throw error(m_archive);
    }
    return std::string(name);
}


bool archive::is_dir_entry(entry_index index) const
{
    assert(is_open());
    struct zip_stat zs;
    if (zip_stat_index(m_archive, index, 0, &zs)) {
        throw error(m_archive);
    }
    if ((zs.valid & ZIP_STAT_NAME) && (zs.valid & ZIP_STAT_SIZE) && (zs.valid & ZIP_STAT_CRC)) {
        const auto nameLen = std::strlen(zs.name);
        return zs.name[nameLen - 1] == '/' && zs.size == 0 && zs.crc == 0;
    } else {
        throw error("Cannot determine entry type");
    }
}


namespace
{
void copy_to_stream(
    auto_zip_file& source,
    std::ostream& target,
    std::vector<char>& buffer)
{
    assert(target.good());
    assert(!buffer.empty());
    for (;;) {
        const auto n = source.read(buffer.data(), buffer.size());
        if (n == 0) break;
        target.write(buffer.data(), n);
    }
}

void extract_file_as(
    ::zip* archive,
    entry_index index,
    const boost::filesystem::path& targetPath,
    std::vector<char>& buffer)
{
    assert(archive != nullptr);
    assert(!targetPath.empty());
    assert(!buffer.empty());

    auto_zip_file srcFile(archive, index, 0);
    std::ofstream tgtFile(
        targetPath.string(),
        std::ios_base::binary | std::ios_base::trunc);
    if (!tgtFile.is_open()) {
        throw std::system_error(
            errno,
            std::generic_category(),
            targetPath.string());
    }
    copy_to_stream(srcFile, tgtFile, buffer);
    if (tgtFile.fail()) {
        throw std::system_error(
            make_error_code(std::errc::io_error),
            targetPath.string());
    }
}
} // namespace


void archive::extract_all(
    const boost::filesystem::path& targetDir) const
{
    assert(is_open());
    if (!boost::filesystem::exists(targetDir) ||
        !boost::filesystem::is_directory(targetDir)) {
        throw std::system_error(
            make_error_code(std::errc::not_a_directory),
            targetDir.string());
    }

    auto buffer = std::vector<char>(4096 * 16);
    const auto entryCount = entry_count();
    for (entry_index index = 0; index < entryCount; ++index) {
        const auto entryName = entry_name(index);
        if (!entryName.empty()) {
            const auto entryPath = boost::filesystem::path(entryName);
            if (entryPath.has_root_path()) {
                throw error(
                    "Archive contains an entry with an absolute path: " + entryName);
            }
            const auto targetPath = targetDir / entryPath;
            if (entryName.back() == '/') {
                boost::filesystem::create_directories(targetPath);
            } else {
                boost::filesystem::create_directories(targetPath.parent_path());
                extract_file_as(m_archive, index, targetPath, buffer);
            }
        }
    }
}


boost::filesystem::path archive::extract_file_to(
    entry_index index,
    const boost::filesystem::path& targetDir) const
{
    assert(is_open());
    const auto entryPath = boost::filesystem::path(entry_name(index));
    const auto targetPath = targetDir / entryPath.filename();
    auto buffer = std::vector<char>(4096 * 16);
    extract_file_as(m_archive, index, targetPath, buffer);
    return targetPath;
}


// =============================================================================
// error
// =============================================================================

error::error(const std::string& msg) noexcept
    : cosim::error(make_error_code(errc::zip_error), msg)
{
}

error::error(::zip* archive) noexcept
    : cosim::error(make_error_code(errc::zip_error), zip_strerror(archive))
{
}

error::error(zip_file* file) noexcept
    : cosim::error(make_error_code(errc::zip_error), zip_file_strerror(file))
{
}


} // namespace zip
} // namespace utility
} // namespace cse
