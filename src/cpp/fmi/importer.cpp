#include "cse/fmi/importer.hpp"

#include "cse/error.hpp"
#include "cse/fmi/fmilib.h"
#include "cse/fmi/v1/fmu.hpp"
#include "cse/fmi/v2/fmu.hpp"
#include "cse/log/logger.hpp"
#include "cse/utility/concurrency.hpp"
#include "cse/utility/filesystem.hpp"
#include "cse/utility/uuid.hpp"
#include "cse/utility/zip.hpp"

#include <boost/algorithm/string/trim.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <gsl/gsl_util>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <new>
#include <sstream>
#include <string>

namespace cse
{
namespace fmi
{

/*  A note on the `importer` cache directory structure:
 *
 *  Each `importer` is associated with one top-level cache directory, which
 *  may be shared between several instances in separate fibers, threads and
 *  processes.  The path to this directory is passed to the `importer`
 *  constructor.  This directory has the following substructure:
 *
 *  <cacheDir>/
 *      fmus.lock
 *      fmus/
 *          <FMU 1 GUID>.lock
 *          <FMU 1 GUID>/
 *              modelDescription.xml
 *              binaries/
 *              ...
 *          <FMU 2 GUID>.lock
 *          <FMU 2 GUID>/
 *              modelDescription.xml
 *              binaries/
 *              ...
 *          ...
 *      tmp.lock
 *      tmp/
 *          <randomly named subdirectory 1>/
 *          <randomly named subdirectory 2>/
 *          ...
 *
 *  `fmus/` contains the complete contents of previously-unpacked FMUs.
 *  `tmp/` contains temporary "scratch space" directories.
 *
 *  The files with a `.lock` suffix are lock files associated with the
 *  eponymous directory.  They can be locked in exclusive or shared mode, as
 *  necessary, to prevent the modification or removal of their associated
 *  directory while it is in use.
 */

std::shared_ptr<importer> importer::create(
    const boost::filesystem::path& cachePath)
{
    return std::shared_ptr<importer>(new importer(cachePath));
}


std::shared_ptr<importer> importer::create()
{
    return std::shared_ptr<importer>(new importer(utility::temp_dir()));
}


namespace
{
log::severity_level convert_log_level(jm_log_level_enu_t jmLogLevel)
{
    switch (jmLogLevel) {
        case jm_log_level_fatal:
        case jm_log_level_error:
            return log::error;
        case jm_log_level_warning:
            return log::warning;
        case jm_log_level_info:
            return log::info;
        case jm_log_level_verbose:
        case jm_log_level_debug:
        case jm_log_level_nothing:
        case jm_log_level_all:
        default:
            // The last two cases + default should never match, and if
            // they do, we at least make sure a message is printed in
            // debug mode.
            return log::debug;
    }
}

void logger_callback(
    jm_callbacks* /*callbacks*/,
    jm_string module,
    jm_log_level_enu_t logLevel,
    jm_string message)
{
    const auto myLevel = convert_log_level(logLevel);
    // Errors are dealt with with exceptions
    BOOST_LOG_SEV(log::logger(), myLevel)
        << "[FMI Library: " << module << "] " << message;
}

std::unique_ptr<jm_callbacks> make_callbacks()
{
    auto c = std::make_unique<jm_callbacks>();
    std::memset(c.get(), 0, sizeof(jm_callbacks));
    c->malloc = std::malloc;
    c->calloc = std::calloc;
    c->realloc = std::realloc;
    c->free = std::free;
    c->logger = &logger_callback;
    c->log_level = jm_log_level_all;
    c->context = nullptr;
    std::memset(c->errMessageBuffer, 0, JM_MAX_ERROR_MESSAGE_SIZE);
    return c;
}
} // namespace


importer::importer(const boost::filesystem::path& cachePath)
    : callbacks_{make_callbacks()}
    , handle_{fmi_import_allocate_context(callbacks_.get()), &fmi_import_free_context}
    , fmuDir_{cachePath / "fmu"}
    , workDir_{cachePath / "tmp"}
{
    if (handle_ == nullptr) throw std::bad_alloc();
}


importer::importer(utility::temp_dir&& tempDir)
    : importer{tempDir.path()}
{
    tempCacheDir_ = std::make_unique<utility::temp_dir>(std::move(tempDir));
}


namespace
{
struct minimal_model_description
{
    fmi_version fmiVersion;
    std::string guid;
};

// Reads the 'fmiVersion' and 'guid' attributes from the XML file.
minimal_model_description peek_model_description(
    const boost::filesystem::path& fmuUnpackDir)
{
    const auto xmlFile = fmuUnpackDir / "modelDescription.xml";
    boost::property_tree::ptree xml;
    boost::property_tree::read_xml(xmlFile.string(), xml);

    minimal_model_description md;

    auto fmiVersion = xml.get(
        "fmiModelDescription.<xmlattr>.fmiVersion",
        std::string());
    boost::trim(fmiVersion);
    if (fmiVersion.empty()) {
        throw error(
            make_error_code(errc::bad_file),
            "Invalid modelDescription.xml; fmiVersion attribute missing or empty");
    }
    if (fmiVersion.size() >= 3 && fmiVersion.substr(0, 3) == "1.0") {
        md.fmiVersion = fmi_version::v1_0;
    } else if (fmiVersion.size() >= 3 && fmiVersion.substr(0, 3) == "2.0") {
        md.fmiVersion = fmi_version::v2_0;
    } else {
        md.fmiVersion = fmi_version::unknown;
    }

    if (md.fmiVersion != fmi_version::unknown) {
        md.guid = xml.get(
            "fmiModelDescription.<xmlattr>.guid",
            std::string());
        boost::trim(md.guid);
        if (md.guid.empty()) {
            throw error(
                make_error_code(errc::bad_file),
                "Invalid modelDescription.xml; guid attribute missing or empty");
        }
    }
    return md;
}

// Replaces all characters which are not printable ASCII characters or
// not valid for use in a path with their percent-encoded equivalents.
// References:
//     https://en.wikipedia.org/wiki/Percent-encoding
//     https://msdn.microsoft.com/en-us/library/aa365247.aspx
std::string sanitise_path(const std::string& str)
{
    CSE_INPUT_CHECK(!str.empty());
    std::ostringstream sanitised;
    sanitised.fill('0');
    sanitised << std::hex;
    for (const char c : str) {
        if (c < 0x20 || c > 0x7E
#ifdef _WIN32
            || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' || c == '?' || c == '*'
#endif
        ) {
            sanitised
                << '%'
                << std::setw(2)
                << static_cast<int>(static_cast<unsigned char>(c));
        } else {
            sanitised << c;
        }
    }
    auto sanitised_string = sanitised.str();
    sanitised_string.erase(std::remove(sanitised_string.begin(), sanitised_string.end(), '{'), sanitised_string.end());
    sanitised_string.erase(std::remove(sanitised_string.begin(), sanitised_string.end(), '}'), sanitised_string.end());
    return sanitised_string;
}


// Returns the name of a lock file associated with `path`.
boost::filesystem::path lock_file_name(const boost::filesystem::path& path)
{
    return path.string() + ".lock";
}


// Creates a lock file along with its parent directories, and returns a
// `file_lock` for it.
utility::file_lock create_lock_file(
    const boost::filesystem::path& path,
    utility::file_lock_initial_state initialState)
{
    boost::filesystem::create_directories(path.parent_path());
    return utility::file_lock(path, initialState);
}

} // namespace


std::shared_ptr<fmu> importer::import(const boost::filesystem::path& fmuPath)
{
    prune_ptr_caches();
    auto pit = pathCache_.find(fmuPath);
    if (pit != end(pathCache_)) return pit->second.lock();

    // Lock the temporary working directory. We only need a shared lock to
    // to prevent its deletion by `clean_cache()`.  Since we use UUIDs for
    // the individual subfolders, there's no chance of other conflicts.
    auto workDirLock = create_lock_file(
        lock_file_name(workDir_),
        utility::file_lock_initial_state::locked_shared);

    // Unzip the model description into a temporary folder
    const auto zip = cse::utility::zip::archive(fmuPath);
    const auto tempMdDir = workDir_ / cse::utility::random_uuid();
    boost::filesystem::create_directories(tempMdDir);
    const auto removeTempMdDir = gsl::finally([tempMdDir]() {
        boost::system::error_code errorCode;
        boost::filesystem::remove_all(tempMdDir, errorCode);
    });
    const auto modelDescriptionIndex = zip.find_entry("modelDescription.xml");
    if (modelDescriptionIndex == cse::utility::zip::invalid_entry_index) {
        throw error(
            make_error_code(errc::bad_file),
            fmuPath.string() + " does not contain modelDescription.xml");
    }
    zip.extract_file_to(modelDescriptionIndex, tempMdDir);

    // Look at the model description to figure out the FMU's GUID.
    const auto minModelDesc = peek_model_description(tempMdDir);
    if (minModelDesc.fmiVersion == fmi_version::unknown) {
        throw error(
            make_error_code(errc::unsupported_feature),
            "Unsupported FMI version for FMU '" + fmuPath.string() + "'");
    }

    // If we have already loaded this FMU, return our existing instance.
    auto git = guidCache_.find(minModelDesc.guid);
    if (git != end(guidCache_)) return git->second.lock();

    // Lock the directory that holds all FMUs.  We only need a shared lock to
    // to prevent its deletion by `clean_cache()`, since the individual FMU
    // directories are protected by non-shared locks.
    auto fmuDirLock = create_lock_file(
        lock_file_name(fmuDir_),
        utility::file_lock_initial_state::locked_shared);

    // Lock the directory that holds (or will hold) the entire FMU contents.
    const auto fmuUnpackDir = fmuDir_ / sanitise_path(minModelDesc.guid);
    const auto fmuUnpackDirLockFile = lock_file_name(fmuUnpackDir);
    auto fmuUnpackDirLock = create_lock_file(
        fmuUnpackDirLockFile,
        utility::file_lock_initial_state::locked);

    // Unzip the entire FMU if necessary.
    const auto modelDescriptionPath = fmuUnpackDir / "modelDescription.xml";
    if (!boost::filesystem::exists(modelDescriptionPath) ||
        boost::filesystem::last_write_time(fmuPath) > boost::filesystem::last_write_time(modelDescriptionPath)) {
        try {
            boost::filesystem::create_directories(fmuUnpackDir);
            zip.extract_all(fmuUnpackDir);
        } catch (...) {
            boost::system::error_code errorCode;
            boost::filesystem::remove_all(fmuUnpackDir, errorCode);
            throw;
        }
    }

    // Unlock the FMU directory again, so the `fmu` object can lock it.
    fmuUnpackDirLock.unlock();

    // Create and return an `fmu` object.
    auto fmuObj = minModelDesc.fmiVersion == fmi_version::v1_0
        ? std::shared_ptr<fmu>(new v1::fmu(shared_from_this(), fmuUnpackDir, &fmuUnpackDirLockFile))
        : std::shared_ptr<fmu>(new v2::fmu(shared_from_this(), fmuUnpackDir, &fmuUnpackDirLockFile));
    pathCache_[fmuPath] = fmuObj;
    guidCache_[minModelDesc.guid] = fmuObj;
    return fmuObj;
}


std::shared_ptr<fmu> importer::import_unpacked(
    const boost::filesystem::path& unpackedFMUPath)
{
    prune_ptr_caches();
    const auto minModelDesc = peek_model_description(unpackedFMUPath);
    if (minModelDesc.fmiVersion == fmi_version::unknown) {
        throw error(
            make_error_code(errc::unsupported_feature),
            "Unsupported FMI version for FMU at '" + unpackedFMUPath.string() + "'");
    }
    auto git = guidCache_.find(minModelDesc.guid);
    if (git != end(guidCache_)) return git->second.lock();

    auto fmuObj = minModelDesc.fmiVersion == fmi_version::v1_0
        ? std::shared_ptr<fmu>(new v1::fmu(shared_from_this(), unpackedFMUPath, nullptr))
        : std::shared_ptr<fmu>(new v2::fmu(shared_from_this(), unpackedFMUPath, nullptr));
    guidCache_[minModelDesc.guid] = fmuObj;
    return fmuObj;
}


void importer::clean_cache()
{
    namespace fs = boost::filesystem;

    // Remove unused FMUs
    auto fmuDirLock = create_lock_file(
        lock_file_name(fmuDir_),
        utility::file_lock_initial_state::locked);

    if (fs::exists(fmuDir_)) {
        boost::system::error_code errorCode;
        for (auto it = fs::directory_iterator(fmuDir_); it != fs::directory_iterator(); ++it) {
            if (it->status().type() == fs::directory_file &&
                guidCache_.count(it->path().filename().string()) == 0) {
                const auto fmuUnpackDirLockFile = lock_file_name(it->path());
                auto fmuUnpackDirLock = create_lock_file(
                    fmuUnpackDirLockFile,
                    utility::file_lock_initial_state::locked);
                fs::remove_all(*it, errorCode);
                fs::remove(fmuUnpackDirLockFile);
            }
        }
    }

    // Delete the temp-files directory
    auto workDirLock = create_lock_file(
        lock_file_name(workDir_),
        utility::file_lock_initial_state::locked);
    boost::system::error_code errorCode;
    boost::filesystem::remove_all(workDir_, errorCode);
}


std::string importer::last_error_message()
{
    return std::string(jm_get_last_error(callbacks_.get()));
}


fmi_import_context_t* importer::fmilib_handle() const
{
    return handle_.get();
}


void importer::prune_ptr_caches()
{
    for (auto it = begin(pathCache_); it != end(pathCache_);) {
        if (it->second.expired())
            pathCache_.erase(it++);
        else
            ++it;
    }
    for (auto it = begin(guidCache_); it != end(guidCache_);) {
        if (it->second.expired())
            guidCache_.erase(it++);
        else
            ++it;
    }
}


} // namespace fmi
} // namespace cse
