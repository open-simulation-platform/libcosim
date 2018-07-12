/**
 * \file
 * ZIP file manipulation
 */
#ifndef CSE_UTILITY_ZIP_HPP
#define CSE_UTILITY_ZIP_HPP

#include <cstdint>
#include <filesystem>
#include <string>

#include <cse/exception.hpp>


// Forward declarations to avoid dependency on zip.h
struct zip;
struct zip_file;


namespace cse
{
namespace utility
{

/// Utilities for dealing with ZIP archives.
namespace zip
{

/**
 *  A type for numeric zip entry indices.
 *  \see archive
 *  \see invalid_entry_index
 */
using entry_index = std::uint64_t;


/**
 *  An index value that represents an invalid/unknown zip entry.
 *  \see archive
 */
constexpr entry_index invalid_entry_index = 0xFFFFFFFFFFFFFFFFull;


/**
 *  A class for reading ZIP archives.
 *  
 *  Currently, only a limited set of reading operations are supported, and no
 *  writing/modification operations.
 *  
 *  A ZIP archive is organised as a number of *entries*, where each entry is a
 *  file or a directory.  Each entry has a unique integer index, and the indices
 *  run consecutively from 0 through `entry_count()-1`.  For example, a file with
 *  2 file entries and 1 directory entry, i.e. `entry_count() == 3`, could look
 *  like this:
 *  
 *      Index  Name
 *      -----  ----------------
 *          0  readme.txt
 *          1  images/
 *          2  images/photo.jpg
 *  
 */
class archive
{
public:
    /// Default constructor; does not associate the object with an archive file.
    archive() noexcept;

    /**
     *  Constructor which opens a ZIP archive.
     *
     *  This is equivalent to default construction followed by a call to `open()`.
     *
     *  \param [in] path
     *      The path to a ZIP archive file.
     *
     *  \throws cse::utility::zip::error
     *      If there was an error opening the archive.
     */
    explicit archive(const std::filesystem::path& path);

    // Disable copying.
    archive(const archive&) = delete;
    archive& operator=(const archive&) = delete;

    /// Move constructor.
    archive(archive&&) noexcept;
    /// Move assignment operator.
    archive& operator=(archive&&) noexcept;

    /// Destructor; calls discard().
    ~archive() noexcept;

    /**
     *  Opens a ZIP archive.
     *
     *  \param [in] path
     *      The path to a ZIP archive file.
     *  \throws cse::utility::zip::error
     *      If there was an error opening the archive.
     *  \pre
     *      `is_open() == false`
     */
    void open(const std::filesystem::path& path);

    /**
     *  Closes the archive.
     *
     *  If no archive is open, this function has no effect.
     */
    void discard() noexcept;

    /// Returns whether this object refers to an open ZIP archive.
    bool is_open() const noexcept;

    /**
     *  Returns the number of entries in the archive.
     *
     *  This includes both files and directories.
     *
     *  \pre `is_open() == true`
     */
    std::uint64_t entry_count() const;

    /**
     *  Finds an entry by name.
     *
     *  \param [in] name
     *      The full name of a file or directory in the archive.  The search is
     *      case sensitive, and directory names must end with a forward slash (/).
     *  \returns
     *      The index of the entry with the given name, or `invalid_entry_index`
     *      if no such entry was found.
     *  \throws cse::utility::zip::error
     *      If there was an error accessing the archive.
     *  \pre
     *      `is_open() == true`
     */
    entry_index find_entry(const std::string& name) const;

    /**
     *  Returns the name of an archive entry.
     *
     *  \param [in] index
     *      An archive entry index in the range `[0,entry_count())`.
     *  \returns
     *      The full name of the entry with the given index.
     *  \throws cse::utility::zip::error
     *      If there was an error accessing the archive.
     *  \pre
     *      `is_open() == true`
     */
    std::string entry_name(entry_index index) const;

    /**
     *  Returns whether an archive entry is a directory.
     *
     *  This returns `true` if and only if the entry has zero size, has a CRC of
     *  zero, and a name which ends with a forward slash (/).
     *
     *  \param [in] index
     *      An archive entry index in the range `[0,entry_count())`.
     *  \returns
     *      Whether the entry with the given index is a directory.
     *  \throws cse::utility::zip::error
     *      If there was an error accessing the archive.
     *  \pre
     *      `is_open() == true`
     */
    bool is_dir_entry(entry_index index) const;

    /**
     *  Extracts the entire contents of the archive.
     *
     *  This will extract all entries in the archive to the given target directory,
     *  recreating the subdirectory structure in the archive.
     *
     *  \param [in] targetDir
     *      The directory to which the files should be extracted.
     *  \throws cse::utility::zip::error
     *      If there was an error accessing the archive.
     *  \throws std::system_error
     *      On I/O error.
     *  \pre
     *      `is_open() == true`
     */
    void extract_all(const std::filesystem::path& targetDir) const;

    /**
     *  Extracts a single file from the archive, placing it in a specific
     *  target directory.
     *
     *  This ignores the directory structure *inside* the archive, i.e. the file
     *  will always be created directly under the given target directory.
     *
     *  \param [in] index
     *      An archive entry index in the range `[0,entry_count())`.
     *  \param [in] targetDir
     *      The directory to which the file should be extracted.
     *
     *  \returns
     *      The full path to the extracted file, i.e. the base name of the archive
     *      entry appended to the target directory.
     *  \throws cse::utility::zip::error
     *      If there was an error accessing the archive.
     *  \throws std::system_error
     *      On I/O error.
     *  \pre
     *      `is_open() == true`
     */
    std::filesystem::path extract_file_to(
        entry_index index,
        const std::filesystem::path& targetDir) const;

private:
    ::zip* m_archive;
};


/// Exception class for errors that occur while dealing with ZIP files.
class error : public cse::error
{
public:
    /**
     *  \internal
     *  Creates an exception with the given message.
     */
    explicit error(const std::string& msg) noexcept;

    /**
     *  \internal
     *  Creates an exception for the last error for the given archive.
     */
    explicit error(::zip* archive) noexcept;

    /**
     *  \internal
     *  Creates an exception for the last error for the given file.
     */
    explicit error(::zip_file* file) noexcept;
};


}}} // namespace
#endif // header guard
