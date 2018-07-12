/**
 *  \file
 *  File system utilities.
 */
#ifndef CSE_UTILITY_FILESYSTEM_HPP
#define CSE_UTILITY_FILESYSTEM_HPP

#include <filesystem>
#include <cse/config.hpp>


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
        const std::filesystem::path& parent = std::filesystem::path());

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
    const std::filesystem::path& path() const;

private:
    void delete_noexcept() noexcept;

    std::filesystem::path path_;
};


}} // namespace
#endif // header guard
