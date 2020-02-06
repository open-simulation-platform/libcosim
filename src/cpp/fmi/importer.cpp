#include "cse/fmi/importer.hpp"

#include "cse/error.hpp"
#include "cse/fmi/fmilib.h"
#include "cse/fmi/v1/fmu.hpp"
#include "cse/fmi/v2/fmu.hpp"
#include "cse/log/logger.hpp"
#include "cse/utility/filesystem.hpp"
#include "cse/utility/zip.hpp"

#include <boost/algorithm/string/trim.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <gsl/gsl_util>

#include <cstdlib>
#include <cstring>
#include <new>


namespace cse
{
namespace fmi
{


std::shared_ptr<importer> importer::create(std::shared_ptr<file_cache> cache)
{
    return std::shared_ptr<importer>(new importer(cache));
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


importer::importer(std::shared_ptr<file_cache> cache)
    : fileCache_(cache)
    , callbacks_(make_callbacks())
    , handle_(fmi_import_allocate_context(callbacks_.get()), &fmi_import_free_context)
{
    if (handle_ == nullptr) throw std::bad_alloc();
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

bool is_outdated(
    const boost::filesystem::path& file,
    const boost::filesystem::path& comparator)
{
    return !boost::filesystem::exists(file) ||
        boost::filesystem::last_write_time(comparator) > boost::filesystem::last_write_time(file);
}

} // namespace


std::shared_ptr<fmu> importer::import(const boost::filesystem::path& fmuPath)
{
    prune_ptr_caches();
    auto pit = pathCache_.find(fmuPath);
    if (pit != end(pathCache_)) return pit->second.lock();

    // Unzip the model description into a temporary folder
    const auto tempMdDir = utility::temp_dir();
    const auto zip = cse::utility::zip::archive(fmuPath);
    const auto modelDescriptionIndex = zip.find_entry("modelDescription.xml");
    if (modelDescriptionIndex == cse::utility::zip::invalid_entry_index) {
        throw error(
            make_error_code(errc::bad_file),
            fmuPath.string() + " does not contain modelDescription.xml");
    }
    zip.extract_file_to(modelDescriptionIndex, tempMdDir.path());

    // Look at the model description to figure out the FMU's GUID.
    const auto minModelDesc = peek_model_description(tempMdDir.path());
    if (minModelDesc.fmiVersion == fmi_version::unknown) {
        throw error(
            make_error_code(errc::unsupported_feature),
            "Unsupported FMI version for FMU '" + fmuPath.string() + "'");
    }

    // If we have already loaded this FMU, return our existing instance.
    auto git = guidCache_.find(minModelDesc.guid);
    if (git != end(guidCache_)) return git->second.lock();

    // Get a cache directory to hold the entire FMU contents.
    auto fmuUnpackDir = fileCache_->get_directory_rw(minModelDesc.guid);

    // Unzip the entire FMU if necessary.
    const auto modelDescriptionPath = fmuUnpackDir->path() / "modelDescription.xml";
    if (is_outdated(modelDescriptionPath, fmuPath)) {
        try {
            zip.extract_all(fmuUnpackDir->path());
        } catch (...) {
            // Remove model description again, so we don't erroneously think
            // that the unpacking was successful the next time we try it.
            boost::system::error_code ignoredError;
            boost::filesystem::remove(modelDescriptionPath, ignoredError);
            throw;
        }
    }

    // Drop R/W privileges and acquire read-only access to FMU cache directory
    fmuUnpackDir.reset();
    auto fmuUnpackDirRO = fileCache_->get_directory_ro(minModelDesc.guid);

    // Create and return an `fmu` object.
    auto fmuObj = minModelDesc.fmiVersion == fmi_version::v1_0
        ? std::shared_ptr<fmu>(new v1::fmu(shared_from_this(), std::move(fmuUnpackDirRO)))
        : std::shared_ptr<fmu>(new v2::fmu(shared_from_this(), std::move(fmuUnpackDirRO)));
    pathCache_[fmuPath] = fmuObj;
    guidCache_[minModelDesc.guid] = fmuObj;
    return fmuObj;
}


namespace
{
class existing_directory_ro : public file_cache::directory_ro
{
public:
    explicit existing_directory_ro(const boost::filesystem::path& path)
        : path_(path)
    {}

    boost::filesystem::path path() const override { return path_; }

private:
    boost::filesystem::path path_;
};
} // namespace


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

    auto unpackedFMUDir = std::make_unique<existing_directory_ro>(unpackedFMUPath);
    auto fmuObj = minModelDesc.fmiVersion == fmi_version::v1_0
        ? std::shared_ptr<fmu>(new v1::fmu(shared_from_this(), std::move(unpackedFMUDir)))
        : std::shared_ptr<fmu>(new v2::fmu(shared_from_this(), std::move(unpackedFMUDir)));
    guidCache_[minModelDesc.guid] = fmuObj;
    return fmuObj;
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
