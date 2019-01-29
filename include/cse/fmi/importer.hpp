/**
 *  \file
 *  FMU import functionality.
 */
#ifndef CSE_FMI_IMPORTER_HPP
#define CSE_FMI_IMPORTER_HPP

#include <boost/filesystem.hpp>

#include <map>
#include <memory>
#include <string>


// Forward declarations to avoid external dependency on FMI Library.
struct fmi_xml_context_t;
typedef fmi_xml_context_t fmi_import_context_t;
struct jm_callbacks;


namespace cse
{

namespace utility
{
class temp_dir;
}


namespace fmi
{

class fmu;


/**
 *  Imports and caches FMUs.
 *
 *  The main purpose of this class is to read FMU files and create
 *  `cse::fmi::fmu` objects to represent them.  This is done with the
 *  `import()` function.
 *
 *  An `importer` object uses an on-disk cache that holds the unpacked
 *  contents of previously imported FMUs, so that they don't need to be
 *  unpacked anew every time they are imported.  This is a huge time-saver
 *  when large and/or many FMUs are loaded.  The path to this cache may be
 *  supplied by the user, in which case it is not automatically emptied on
 *  destruction.  Thus, if the same path is supplied each time, the cache
 *  becomes persistent between program runs.  It may be cleared manually
 *  by calling `clean_cache()`.
 *
 *  \warning
 *      Currently, there are no synchronisation mechanisms to protect the
 *      cache from concurrent use, so accessing the same cache from
 *      multiple instances/processes will likely cause problems.
 */
class importer : public std::enable_shared_from_this<importer>
{
public:
    /**
     *  Creates a new FMU importer that uses a specific cache
     *  directory.
     *
     *  The cache directory will not be removed or emptied on destruction.
     *
     *  \param [in] cachePath
     *      The path to the directory which will hold the FMU cache.
     *      If it does not exist already, it will be created.
     */
    static std::shared_ptr<importer> create(
        const boost::filesystem::path& cachePath);

    /**
     *  Creates a new FMU importer that uses a temporary cache
     *          directory.
     *
     *  A new cache directory will be created in a location suitable for
     *  temporary files under the conventions of the operating system.
     *  It will be completely removed again on destruction.
    */
    static std::shared_ptr<importer> create();

private:
    // Private constructors, to force use of factory functions.
    importer(const boost::filesystem::path& cachePath);
    importer(utility::temp_dir&& tempDir);

public:
    /**
     *  Imports and loads an FMU.
     *
     *  Loaded FMUs are managed using reference counting.  If an FMU is loaded,
     *  and then the same FMU is loaded again before the first one has been
     *  destroyed, the second call will return a reference to the first one.
     *  (Two FMUs are deemed to be the same if they have the same path *or* the
     *  same GUID.)
     *
     *  \param [in] fmuPath
     *      The path to the FMU file.
     *  \returns
     *      An object which represents the imported FMU.
     */
    std::shared_ptr<fmu> import(const boost::filesystem::path& fmuPath);

    /**
     *  Imports and loads an FMU that has already been unpacked.
     *
     *  This is more or less equivalent to `import()`, but since the FMU is
     *  already unpacked its contents will be read from the specified directory
     *  rather than the cache.
     *
     *  \param [in] unpackedFMUPath
     *      The path to a directory that holds the unpacked contents of an FMU.
     *  \returns
     *      An object which represents the imported FMU.
     */
    std::shared_ptr<fmu> import_unpacked(
        const boost::filesystem::path& unpackedFMUPath);

    /**
     *  Removes unused files and directories from the FMU cache.
     *
     *  This will remove all FMU contents from the cache, except the ones for
     *  which there currently exist FMU objects.
     */
    void clean_cache();

    /// Returns the last FMI Library error message.
    std::string last_error_message();

    /// Returns a pointer to the underlying FMI Library import context.
    fmi_import_context_t* fmilib_handle() const;

private:
    void prune_ptr_caches();

    // Note: The order of these declarations is important!
    std::unique_ptr<utility::temp_dir> tempCacheDir_; // Only used when no cache dir is given
    std::unique_ptr<jm_callbacks> callbacks_;
    std::unique_ptr<fmi_import_context_t, void (*)(fmi_import_context_t*)> handle_;

    boost::filesystem::path fmuDir_;
    boost::filesystem::path workDir_;

    std::map<boost::filesystem::path, std::weak_ptr<fmu>> pathCache_;
    std::map<std::string, std::weak_ptr<fmu>> guidCache_;
};


} // namespace fmi
} // namespace cse
#endif // header guard
