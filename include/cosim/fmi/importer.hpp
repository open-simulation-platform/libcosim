/**
 *  \file
 *  FMU import functionality.
 *
 *  \copyright
 *      This Source Code Form is subject to the terms of the Mozilla Public
 *      License, v. 2.0. If a copy of the MPL was not distributed with this
 *      file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#ifndef COSIM_FMI_IMPORTER_HPP
#define COSIM_FMI_IMPORTER_HPP

#include <cosim/file_cache.hpp>

#include <boost/filesystem.hpp>

#include <map>
#include <memory>
#include <string>


// Forward declarations to avoid external dependency on FMI Library.
struct fmi_xml_context_t;
typedef fmi_xml_context_t fmi_import_context_t;
struct jm_callbacks;


namespace cosim
{
namespace fmi
{

class fmu;


/**
 *  Imports and caches FMUs.
 *
 *  The main purpose of this class is to read FMU files and create
 *  `cosim::fmi::fmu` objects to represent them.  This is done with the
 *  `import()` function.
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
     *  \param [in] cache
     *      The cache to which FMUs will be unpacked.
     *      By default, a non-persistent cache is used.
     */
    static std::shared_ptr<importer> create(
        std::shared_ptr<file_cache> cache = std::make_shared<temporary_file_cache>());

private:
    // Private constructors, to force use of factory functions.
    explicit importer(std::shared_ptr<file_cache> cache);

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
     *  rather than the cache.  (The contents will not be copied to the cache.)
     *
     *  \param [in] unpackedFMUPath
     *      The path to a directory that holds the unpacked contents of an FMU.
     *  \returns
     *      An object which represents the imported FMU.
     */
    std::shared_ptr<fmu> import_unpacked(
        const boost::filesystem::path& unpackedFMUPath);

    /// Returns the last FMI Library error message.
    std::string last_error_message();

    /// Returns a pointer to the underlying FMI Library import context.
    fmi_import_context_t* fmilib_handle() const;

private:
    void prune_ptr_caches();

    // Note: The order of these declarations is important!
    std::shared_ptr<file_cache> fileCache_;
    std::unique_ptr<jm_callbacks> callbacks_;
    std::unique_ptr<fmi_import_context_t, void (*)(fmi_import_context_t*)> handle_;

    std::map<boost::filesystem::path, std::weak_ptr<fmu>> pathCache_;
    std::map<std::string, std::weak_ptr<fmu>> guidCache_;
};


} // namespace fmi
} // namespace cosim
#endif // header guard
