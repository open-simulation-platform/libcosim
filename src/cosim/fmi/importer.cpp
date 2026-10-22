/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/fmi/importer.hpp"

#include "cosim/error.hpp"
#include "cosim/fmi/fmilib.h"
#include "cosim/fmi/v1/fmu.hpp"
#include "cosim/fmi/v2/fmu.hpp"
#include "cosim/log/logger.hpp"
#include "cosim/utility/filesystem.hpp"
#include "cosim/utility/zip.hpp"

#include <boost/algorithm/string/trim.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <gsl/util>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <new>
#include <string_view>
#include <system_error>

#if defined(__APPLE__)
#    include <mach-o/fat.h>
#    include <mach-o/loader.h>
#elif defined(__linux__)
#    include <elf.h>
#endif


namespace cosim
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
    std::string coSimulationModelIdentifier; // FMI 2 only
};

// Reads the 'fmiVersion' and 'guid' attributes from the XML file, and for
// FMI 2, the co-simulation model identifier.
minimal_model_description peek_model_description(
    const cosim::filesystem::path& fmuUnpackDir)
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
    if (md.fmiVersion == fmi_version::v2_0) {
        md.coSimulationModelIdentifier = xml.get(
            "fmiModelDescription.CoSimulation.<xmlattr>.modelIdentifier",
            std::string());
        boost::trim(md.coSimulationModelIdentifier);
    }
    return md;
}

bool is_outdated(
    const cosim::filesystem::path& file,
    const cosim::filesystem::path& comparator)
{
    return !cosim::filesystem::exists(file) ||
        cosim::filesystem::last_write_time(comparator) > cosim::filesystem::last_write_time(file);
}

// FMI 2.0.4 only has platform names for x86 (e.g. "darwin64" and "linux64"),
// and uses the FMI 3 platform tuple (e.g. "aarch64-darwin") for other
// platforms. Returns the x86 name that FMUs sometimes use for the latter
// anyway, or an empty string.
constexpr std::string_view x86_platform_name(std::string_view platform)
{
    if (platform == "aarch64-darwin") return "darwin64";
    if (platform == "aarch64-linux") return "linux64";
    return {};
}

#if defined(__APPLE__) || defined(__linux__)
// Returns whether the shared library at `path` contains 64-bit ARM code.
// On macOS, this may be a universal library with an arm64 slice.
bool contains_aarch64_code(const cosim::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    const auto read_uint32 = [&file](bool bigEndian) {
        unsigned char b[4] = {};
        file.read(reinterpret_cast<char*>(b), sizeof b);
        return bigEndian
            ? std::uint32_t{b[0]} << 24 | std::uint32_t{b[1]} << 16 | std::uint32_t{b[2]} << 8 | b[3]
            : std::uint32_t{b[3]} << 24 | std::uint32_t{b[2]} << 16 | std::uint32_t{b[1]} << 8 | b[0];
    };
#    if defined(__APPLE__)
    const auto magic = read_uint32(true);
    if (magic == FAT_MAGIC || magic == FAT_MAGIC_64) {
        // Universal binary; the headers are big-endian.
        const auto archCount = read_uint32(true);
        const auto archSize = magic == FAT_MAGIC ? sizeof(fat_arch) : sizeof(fat_arch_64);
        for (std::uint32_t i = 0; file && i < archCount; ++i) {
            const auto archStart = file.tellg();
            if (read_uint32(true) == static_cast<std::uint32_t>(CPU_TYPE_ARM64)) return true;
            file.seekg(archStart + static_cast<std::streamoff>(archSize));
        }
        return false;
    }
    if (magic == MH_CIGAM_64) {
        // Thin 64-bit binary, little-endian.
        return read_uint32(false) == static_cast<std::uint32_t>(CPU_TYPE_ARM64) && !!file;
    }
    return false;
#    else
    unsigned char ident[EI_NIDENT] = {};
    file.read(reinterpret_cast<char*>(ident), sizeof ident);
    if (!file || std::memcmp(ident, ELFMAG, SELFMAG) != 0 ||
        ident[EI_CLASS] != ELFCLASS64 || ident[EI_DATA] != ELFDATA2LSB) {
        return false;
    }
    // e_type and e_machine follow e_ident, both 16 bits.
    const auto typeAndMachine = read_uint32(false);
    return !!file && (typeAndMachine >> 16) == EM_AARCH64;
#    endif
}
#endif

// FMI 2.0.4 reserves names like "darwin64" and "linux64" for 64-bit x86, so
// e.g. FMI 2 FMUs keep their Apple silicon binaries in "aarch64-darwin",
// which is where FMI Library looks for them on such machines. Many FMUs only
// have a universal (or even an arm64) library in "darwin64", though, so if
// the platform folder is missing and the x86 folder contains code for this
// platform, we make the former a link to the latter in the unpacked FMU.
void link_x86_platform_folder_if_platform_folder_missing(
    [[maybe_unused]] const cosim::filesystem::path& fmuPath,
    [[maybe_unused]] const cosim::filesystem::path& fmuUnpackDir,
    [[maybe_unused]] const std::string& modelIdentifier)
{
#if defined(__APPLE__) || defined(__linux__)
    constexpr auto platform = std::string_view(FMI2_PLATFORM);
    constexpr auto x86Platform = x86_platform_name(platform);
    if (x86Platform.empty()) return;
#    if defined(__APPLE__)
    constexpr auto librarySuffix = ".dylib";
#    else
    constexpr auto librarySuffix = ".so";
#    endif

    const auto binariesDir = fmuUnpackDir / "binaries";
    const auto platformDir = binariesDir / platform;
    const auto platformDirStatus = cosim::filesystem::symlink_status(platformDir);
    if (!cosim::filesystem::exists(platformDirStatus)) {
        const auto library = binariesDir / x86Platform / (modelIdentifier + librarySuffix);
        if (!cosim::filesystem::exists(library)) return;
        if (!contains_aarch64_code(library)) {
            BOOST_LOG_SEV(log::logger(), log::warning)
                << "FMU '" << fmuPath.string() << "' has no binaries/" << platform
                << ", and its " << x86Platform << " library contains no code for this platform";
            return;
        }
        std::error_code linkError;
        cosim::filesystem::create_directory_symlink(std::string(x86Platform), platformDir, linkError);
        if (linkError) {
            BOOST_LOG_SEV(log::logger(), log::warning)
                << "FMU '" << fmuPath.string() << "' has no binaries/" << platform
                << ", and linking it to " << x86Platform << " failed: " << linkError.message();
            return;
        }
    } else if (!cosim::filesystem::is_symlink(platformDirStatus)) {
        return;
    }
    BOOST_LOG_SEV(log::logger(), log::warning)
        << "FMU '" << fmuPath.string() << "' has no binaries/" << platform << "; "
        << "using " << x86Platform << ", which FMI 2.0.4 reserves for x86";
#endif
}

// Removes a platform folder link that we have made in an unpacked FMU, so
// that unpacking a new version of the FMU does not write through it.
void remove_platform_folder_link(const cosim::filesystem::path& fmuUnpackDir)
{
    const auto platformDir = fmuUnpackDir / "binaries" / FMI2_PLATFORM;
    std::error_code ignoredError;
    if (cosim::filesystem::is_symlink(cosim::filesystem::symlink_status(platformDir, ignoredError))) {
        cosim::filesystem::remove(platformDir, ignoredError);
    }
}

} // namespace


std::shared_ptr<fmu> importer::import(const cosim::filesystem::path& fmuPath)
{
    prune_ptr_caches();
    auto pit = pathCache_.find(fmuPath);
    if (pit != end(pathCache_)) return pit->second.lock();

    // Unzip the model description into a temporary folder
    const auto tempMdDir = utility::temp_dir();
    const auto zip = cosim::utility::zip::archive(fmuPath);
    const auto modelDescriptionIndex = zip.find_entry("modelDescription.xml");
    if (modelDescriptionIndex == cosim::utility::zip::invalid_entry_index) {
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

    std::string guidStr = std::string(minModelDesc.guid);
    guidStr.erase(std::remove(guidStr.begin(), guidStr.end(), '{'), guidStr.end());
    guidStr.erase(std::remove(guidStr.begin(), guidStr.end(), '}'), guidStr.end());

    auto fmuUnpackDir = fileCache_->get_directory_rw(guidStr);

    // Unzip the entire FMU if necessary.
    const auto modelDescriptionPath = fmuUnpackDir->path() / "modelDescription.xml";
    if (is_outdated(modelDescriptionPath, fmuPath)) {
        remove_platform_folder_link(fmuUnpackDir->path());
        try {
            zip.extract_all(fmuUnpackDir->path());
        } catch (...) {
            // Remove model description again, so we don't erroneously think
            // that the unpacking was successful the next time we try it.
            std::error_code ignoredError;
            cosim::filesystem::remove(modelDescriptionPath, ignoredError);
            throw;
        }
    }
    if (minModelDesc.fmiVersion == fmi_version::v2_0) {
        link_x86_platform_folder_if_platform_folder_missing(
            fmuPath,
            fmuUnpackDir->path(),
            minModelDesc.coSimulationModelIdentifier);
    }

    // Drop R/W privileges and acquire read-only access to FMU cache directory
    fmuUnpackDir.reset();
    auto fmuUnpackDirRO = fileCache_->get_directory_ro(guidStr);

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
    explicit existing_directory_ro(const cosim::filesystem::path& path)
        : path_(path)
    { }

    cosim::filesystem::path path() const override { return path_; }

private:
    cosim::filesystem::path path_;
};
} // namespace


std::shared_ptr<fmu> importer::import_unpacked(
    const cosim::filesystem::path& unpackedFMUPath)
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
} // namespace cosim
