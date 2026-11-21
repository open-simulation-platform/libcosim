/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/fmi/importer.hpp"

#include "cosim/exception.hpp"
#include "cosim/file_cache.hpp"
#include "cosim/fmi/fmilib.h" // IWYU pragma: keep
#include "cosim/fmi/fmu.hpp"
#include "cosim/fmi/v1/fmu.hpp"
#include "cosim/fmi/v2/fmu.hpp"
#include "cosim/fmi/v3/fmu.hpp"
#include "cosim/log/logger.hpp"
#include "cosim/utility/filesystem.hpp"
#include "cosim/utility/zip.hpp"

#include <boost/algorithm/cxx11/all_of.hpp>
#include <boost/algorithm/string/trim.hpp>
#include <boost/log/sources/severity_feature.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/ptree_fwd.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/uuid/detail/sha1.hpp>

#include <FMI/fmi_import_context.h>
#include <JM/jm_callbacks.h>
#include <JM/jm_types.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <memory>
#include <new>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace cosim::fmi
{

std::shared_ptr<importer> importer::create(std::shared_ptr<file_cache> cache)
{
    return std::shared_ptr<importer>(new importer(std::move(cache)));
}

namespace
{
class import_guard
{
public:
    explicit import_guard(std::atomic_flag& flag)
        : flag_(flag)
    {
        if (flag_.test_and_set(std::memory_order_acquire)) {
            throw std::logic_error(
                "Concurrent imports using the same FMI importer are not supported");
        }
    }

    ~import_guard() { flag_.clear(std::memory_order_release); }

    import_guard(const import_guard&) = delete;
    import_guard& operator=(const import_guard&) = delete;
    import_guard(import_guard&&) = delete;
    import_guard& operator=(import_guard&&) = delete;

private:
    std::atomic_flag& flag_;
};

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
            // The last two cases should never match.
            return log::debug;
    }
    return log::debug;
}

void logger_callback(jm_callbacks* /*callbacks*/, jm_string module,
    jm_log_level_enu_t logLevel, jm_string message)
{
    const auto myLevel = convert_log_level(logLevel);
    // Errors are dealt with with exceptions
    BOOST_LOG_SEV(log::logger(), myLevel)
        << "[FMI Library: " << module << "] " << message;
}

std::unique_ptr<jm_callbacks> make_callbacks()
{
    auto callbacks = std::make_unique<jm_callbacks>();
    std::memset(callbacks.get(), 0, sizeof(jm_callbacks));
    callbacks->malloc = std::malloc;
    callbacks->calloc = std::calloc;
    callbacks->realloc = std::realloc;
    callbacks->free = std::free;
    callbacks->logger = &logger_callback;
    callbacks->log_level = jm_log_level_all;
    callbacks->context = nullptr;
    std::fill(std::begin(callbacks->errMessageBuffer),
        std::end(callbacks->errMessageBuffer), '\0');
    return callbacks;
}
} // namespace

importer::importer(std::shared_ptr<file_cache> cache)
    : fileCache_(std::move(cache))
    , callbacks_(make_callbacks())
    , handle_(fmi_import_allocate_context(callbacks_.get()),
          &fmi_import_free_context)
{
    if (handle_ == nullptr) {
        throw std::bad_alloc();
    }
}

namespace
{

struct minimal_model_description
{
    fmi_version fmiVersion = fmi_version::unknown;
    std::string identity;

    // Set if the model description contains a leading '+' in a ModelStructure
    // dependency list; holds the text with the sign removed.
    std::optional<std::string> normalizedModelDescription;
};

std::string read_binary_file(const cosim::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw error(make_error_code(errc::bad_file),
            "Could not read '" + path.string() + "'");
    }
    return {std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>()};
}

void write_binary_file(const cosim::filesystem::path& path,
    const std::string& text)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc); // NOLINT(bugprone-signed-bitwise): Standard stream openmode flags are combined with bitwise OR.
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
    if (!output) {
        throw error(make_error_code(errc::bad_file),
            "Could not write '" + path.string() + "'");
    }
}

bool is_xml_space(char character)
{
    return character == ' ' || character == '\t' || character == '\r' ||
        character == '\n';
}

bool is_digit(char character) { return character >= '0' && character <= '9'; }

std::size_t skip_past(const std::string& text, std::size_t from,
    std::string_view terminator)
{
    const auto position = text.find(terminator, from);
    return position == std::string::npos ? text.size()
                                         : position + terminator.size();
}

// Removes the '+' that starts a token in a list of unsigned integers.
std::string strip_leading_plus_signs(std::string_view list, bool& changed)
{
    std::string result;
    result.reserve(list.size());
    bool tokenStart = true;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const char character = list.at(i);
        if (is_xml_space(character)) {
            tokenStart = true;
        } else {
            const bool sign = tokenStart && character == '+' && i + 1 < list.size() &&
                is_digit(list.at(i + 1));
            tokenStart = false;
            if (sign) {
                changed = true;
                continue;
            }
        }
        result += character;
    }
    return result;
}

struct normalized_attributes
{
    std::size_t end;
    bool selfClosing;
};

std::size_t normalize_attribute_value(
    const std::string& xml, std::size_t position, std::string_view attribute,
    bool normalizeDependencies, std::string& result, bool& changed)
{
    const auto size = xml.size();
    auto valuePosition = position;
    while (valuePosition < size && is_xml_space(xml.at(valuePosition))) {
        ++valuePosition;
    }
    if (valuePosition >= size || xml.at(valuePosition) != '=') {
        return position;
    }
    ++valuePosition;
    while (valuePosition < size && is_xml_space(xml.at(valuePosition))) {
        ++valuePosition;
    }
    if (valuePosition >= size ||
        (xml.at(valuePosition) != '"' && xml.at(valuePosition) != '\'')) {
        return position;
    }

    const char quote = xml.at(valuePosition);
    const auto valueEnd = xml.find(quote, valuePosition + 1);
    if (valueEnd == std::string::npos) {
        result.append(xml, position, std::string::npos);
        return size;
    }
    result.append(xml, position, valuePosition + 1 - position);
    const auto value = std::string_view(xml).substr(
        valuePosition + 1, valueEnd - valuePosition - 1);
    if (normalizeDependencies && attribute == "dependencies") {
        result += strip_leading_plus_signs(value, changed);
    } else {
        result.append(value);
    }
    result += quote;
    return valueEnd + 1;
}

normalized_attributes normalize_tag_attributes(
    const std::string& xml, std::size_t position, std::string& result,
    bool normalizeDependencies, bool& changed)
{
    const auto size = xml.size();
    bool selfClosing = false;
    while (position < size) {
        const char character = xml.at(position);
        if (character == '>') {
            result += character;
            ++position;
            break;
        }
        if (character == '/') {
            selfClosing = true;
        }
        if (character == '/' || is_xml_space(character)) {
            result += character;
            ++position;
            continue;
        }

        const auto attributeStart = position;
        while (position < size && !is_xml_space(xml.at(position)) &&
            xml.at(position) != '=' && xml.at(position) != '>' &&
            xml.at(position) != '/') {
            ++position;
        }
        if (position == attributeStart) {
            result += xml.at(position++);
            continue;
        }
        const auto attribute = std::string_view(xml).substr(
            attributeStart, position - attributeStart);
        result.append(attribute);
        position = normalize_attribute_value(
            xml, position, attribute, normalizeDependencies, result, changed);
    }
    return {position, selfClosing};
}

// The XML Schema allows an optional '+' in front of each index in a
// `dependencies` list, but FMI Library loops forever on it. This rewrites only
// those attribute values inside <ModelStructure>; every other byte, including
// comments, the byte order mark, and line endings, is preserved. Returns
// nothing if no change is needed, so the operation is idempotent.
std::optional<std::string> normalize_dependency_lists(const std::string& xml)
{
    const auto size = xml.size();
    std::string result;
    result.reserve(size);
    bool changed = false;
    bool inModelStructure = false;
    std::size_t position = 0;
    while (position < size) {
        const auto tagStart = xml.find('<', position);
        if (tagStart == std::string::npos) {
            result.append(xml, position, std::string::npos);
            break;
        }
        result.append(xml, position, tagStart - position);
        position = tagStart;

        const auto startsWith = [&](std::string_view prefix) {
            return xml.compare(position, prefix.size(), prefix) == 0;
        };
        std::size_t end = 0;
        if (startsWith("<!--")) {
            end = skip_past(xml, position + 4, "-->");
        } else if (startsWith("<![CDATA[")) {
            constexpr std::string_view cdata_start = "<![CDATA[";
            end = skip_past(xml, position + cdata_start.size(), "]]>");
        } else if (startsWith("<?")) {
            end = skip_past(xml, position + 2, "?>");
        } else if (startsWith("<!")) {
            end = skip_past(xml, position + 2, ">");
        }
        if (end != 0) {
            result.append(xml, position, end - position);
            position = end;
            continue;
        }

        std::size_t tagPosition = position + 1;
        const bool closing = tagPosition < size && xml.at(tagPosition) == '/';
        if (closing) {
            ++tagPosition;
        }
        const auto nameStart = tagPosition;
        while (tagPosition < size && !is_xml_space(xml.at(tagPosition)) &&
            xml.at(tagPosition) != '>' && xml.at(tagPosition) != '/') {
            ++tagPosition;
        }
        const auto name =
            std::string_view(xml).substr(nameStart, tagPosition - nameStart);
        result.append(xml, position, tagPosition - position);
        if (closing && name == "ModelStructure") {
            inModelStructure = false;
        }

        const auto attributes = normalize_tag_attributes(
            xml, tagPosition, result, inModelStructure && !closing, changed);
        if (!closing && !attributes.selfClosing && name == "ModelStructure") {
            inModelStructure = true;
        }
        position = attributes.end;
    }
    if (!changed) {
        return std::nullopt;
    }
    return result;
}

// A leading '+' that survives normalization, for example one written as a
// character reference, would make FMI Library loop forever.
void validate_dependency_lists_for_fmilib(
    const boost::property_tree::ptree& xml)
{
    const auto modelStructure =
        xml.get_child_optional("fmiModelDescription.ModelStructure");
    if (!modelStructure) {
        return;
    }

    const auto check = [](const boost::property_tree::ptree& element) {
        const auto dependencies =
            element.get_optional<std::string>("<xmlattr>.dependencies");
        if (!dependencies) {
            return;
        }
        std::istringstream input(*dependencies);
        std::string token;
        while (input >> token) {
            if (token.size() > 1 && token.front() == '+' &&
                std::all_of(token.begin() + 1, token.end(), is_digit)) {
                throw error(
                    make_error_code(errc::unsupported_feature),
                    "FMI Library cannot parse a leading '+' in a ModelStructure "
                    "dependency list without hanging, and this one cannot be "
                    "normalized");
            }
            if (!boost::algorithm::all_of(token, is_digit)) {
                throw error(make_error_code(errc::bad_file),
                    "Invalid FMI ModelStructure dependency '" + token + "'");
            }
        }
    };

    for (const auto& section : *modelStructure) {
        if (section.first == "<xmlattr>") {
            continue;
        }
        check(section.second);
        for (const auto& element : section.second) {
            if (element.first != "<xmlattr>") {
                check(element.second);
            }
        }
    }
}

// Reads the FMI version and version-specific identity attribute from the XML.
// FMI 3 uses instantiationToken rather than the UUID-shaped FMI 1/2 guid;
// keeping that identity distinct prevents cross-version cache collisions.
minimal_model_description
peek_model_description(const cosim::filesystem::path& fmuUnpackDir)
{
    const auto xmlFile = fmuUnpackDir / "modelDescription.xml";
    boost::property_tree::ptree xml;
    boost::property_tree::read_xml(xmlFile.string(), xml);

    minimal_model_description description;

    auto fmiVersion =
        xml.get("fmiModelDescription.<xmlattr>.fmiVersion", std::string());
    boost::trim(fmiVersion);
    if (fmiVersion.empty()) {
        throw error(
            make_error_code(errc::bad_file),
            "Invalid modelDescription.xml; fmiVersion attribute missing or empty");
    }
    if (fmiVersion.size() >= 3 && fmiVersion.substr(0, 3) == "1.0") {
        description.fmiVersion = fmi_version::v1_0;
    } else if (fmiVersion.size() >= 3 && fmiVersion.substr(0, 3) == "2.0") {
        description.fmiVersion = fmi_version::v2_0;
    } else if (fmiVersion.size() >= 3 && fmiVersion.substr(0, 3) == "3.0") {
        description.fmiVersion = fmi_version::v3_0;
    } else {
        description.fmiVersion = fmi_version::unknown;
    }

    if (description.fmiVersion == fmi_version::v2_0 ||
        description.fmiVersion == fmi_version::v3_0) {
        description.normalizedModelDescription =
            normalize_dependency_lists(read_binary_file(xmlFile));
        if (description.normalizedModelDescription) {
            boost::property_tree::ptree normalizedXml;
            std::istringstream input(*description.normalizedModelDescription);
            boost::property_tree::read_xml(input, normalizedXml);
            validate_dependency_lists_for_fmilib(normalizedXml);
        } else {
            validate_dependency_lists_for_fmilib(xml);
        }
    }

    if (description.fmiVersion != fmi_version::unknown) {
        const auto* const identityAttribute =
            description.fmiVersion == fmi_version::v3_0 ? "instantiationToken"
                                                        : "guid";
        description.identity = xml.get(
            std::string("fmiModelDescription.<xmlattr>.") + identityAttribute,
            std::string());
        if (description.fmiVersion != fmi_version::v3_0) {
            boost::trim(description.identity);
        }
        if (description.identity.empty()) {
            throw error(make_error_code(errc::bad_file),
                "Invalid modelDescription.xml; " +
                    std::string(identityAttribute) +
                    " attribute missing or empty");
        }
    }
    return description;
}

std::string identity_cache_key(const minimal_model_description& description)
{
    return std::to_string(static_cast<int>(description.fmiVersion)) + ":" +
        description.identity;
}

std::string archive_cache_key(const cosim::filesystem::path& path)
{
    constexpr std::size_t hash_buffer_size = 65536;
    constexpr int bits_per_byte = 8;
    constexpr unsigned int hex_nibble_mask = 0x0f;
    std::ifstream input(path, std::ios::binary);
    boost::uuids::detail::sha1 hash;
    std::array<char, hash_buffer_size> buffer{};
    while (input) {
        input.read(buffer.data(), buffer.size());
        hash.process_bytes(buffer.data(), static_cast<std::size_t>(input.gcount()));
    }
    if (!input.eof() || input.bad()) {
        throw error(make_error_code(errc::bad_file),
            "Cannot read FMI 3 archive '" + path.string() + "'");
    }
    boost::uuids::detail::sha1::digest_type digest{};
    hash.get_digest(digest);
    constexpr std::string_view hex = "0123456789abcdef";
    std::string key = "fmu-v3-";
    for (const auto word : digest) {
        for (auto shift = static_cast<int>(sizeof(word) * bits_per_byte) - 4;
            shift >= 0; shift -= 4) {
            key +=
                hex.at((word >> static_cast<unsigned int>(shift)) & hex_nibble_mask);
        }
    }
    return key;
}

std::string directory_cache_key(const minimal_model_description& description)
{
    constexpr unsigned int hex_nibble_mask = 0x0f;
    const auto key = identity_cache_key(description);
    constexpr std::string_view hex = "0123456789abcdef";
    std::string result = "fmu-";
    for (const unsigned char character : key) {
        if ((character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9') || character == '-' ||
            character == '_') {
            result += static_cast<char>(character);
        } else {
            result += '~';
            result += hex.at(static_cast<unsigned int>(character) >> 4U);
            result += hex.at(character & hex_nibble_mask);
        }
    }
    return result;
}

bool is_outdated(const cosim::filesystem::path& file,
    const cosim::filesystem::path& comparator)
{
    return !cosim::filesystem::exists(file) ||
        cosim::filesystem::last_write_time(comparator) >
        cosim::filesystem::last_write_time(file);
}

} // namespace

std::shared_ptr<fmu> importer::import(const cosim::filesystem::path& fmuPath)
{
    const import_guard guard(importInProgress_);
    prune_ptr_caches();
    auto const pit = pathCache_.find(fmuPath);
    if (pit != end(pathCache_)) {
        return pit->second.lock();
    }

    // Unzip the model description into a temporary folder
    const auto tempMdDir = utility::temp_dir();
    const auto zip = cosim::utility::zip::archive(fmuPath);
    const auto modelDescriptionIndex = zip.find_entry("modelDescription.xml");
    if (modelDescriptionIndex == cosim::utility::zip::invalid_entry_index) {
        throw error(make_error_code(errc::bad_file),
            fmuPath.string() + " does not contain modelDescription.xml");
    }
    zip.extract_file_to(modelDescriptionIndex, tempMdDir.path());

    // Look at the model description to determine its FMI identity.
    const auto minModelDesc = peek_model_description(tempMdDir.path());
    if (minModelDesc.fmiVersion == fmi_version::unknown) {
        throw error(make_error_code(errc::unsupported_feature),
            "Unsupported FMI version for FMU '" + fmuPath.string() + "'");
    }

    // If we have already loaded this FMU, return our existing instance.
    // An FMI 3 instantiation token checks XML/binary compatibility, not
    // whether two archives represent the same FMU.
    const bool isFmi3 = minModelDesc.fmiVersion == fmi_version::v3_0;
    const auto identityKey =
        isFmi3 ? archive_cache_key(fmuPath) : identity_cache_key(minModelDesc);
    auto const git = identityCache_.find(identityKey);
    if (git != end(identityCache_)) {
        return git->second.lock();
    }

    // Get a cache directory to hold the entire FMU contents.

    const auto cacheKey =
        isFmi3 ? identityKey : directory_cache_key(minModelDesc);

    auto fmuUnpackDir = fileCache_->get_directory_rw(cacheKey);

    // Unzip the entire FMU if necessary.
    const auto modelDescriptionPath =
        fmuUnpackDir->path() / "modelDescription.xml";
    try {
        if (is_outdated(modelDescriptionPath, fmuPath)) {
            zip.extract_all(fmuUnpackDir->path());
        }
        // FMI Library re-reads the model description from the unpacked
        // directory, so the normalized text must replace the original there.
        if (minModelDesc.normalizedModelDescription &&
            read_binary_file(modelDescriptionPath) !=
                *minModelDesc.normalizedModelDescription) {
            write_binary_file(modelDescriptionPath,
                *minModelDesc.normalizedModelDescription);
        }
    } catch (...) {
        // Remove model description again, so we don't erroneously think
        // that the unpacking was successful the next time we try it.
        std::error_code ignoredError;
        cosim::filesystem::remove(modelDescriptionPath, ignoredError);
        throw;
    }

    // Drop R/W privileges and acquire read-only access to FMU cache directory
    fmuUnpackDir.reset();
    auto fmuUnpackDirRO = fileCache_->get_directory_ro(cacheKey);

    // Dispatch only after the lightweight XML probe. Each implementation then
    // parses its complete model description and advertises its own capabilities.
    // Create and return an `fmu` object.
    std::shared_ptr<fmu> fmuObj;
    if (minModelDesc.fmiVersion == fmi_version::v1_0) {
        fmuObj = std::shared_ptr<fmu>(
            new v1::fmu(shared_from_this(), std::move(fmuUnpackDirRO)));
    } else if (minModelDesc.fmiVersion == fmi_version::v2_0) {
        fmuObj = std::shared_ptr<fmu>(
            new v2::fmu(shared_from_this(), std::move(fmuUnpackDirRO)));
    } else {
        fmuObj = std::shared_ptr<fmu>(
            new v3::fmu(shared_from_this(), std::move(fmuUnpackDirRO)));
    }
    pathCache_[fmuPath] = fmuObj;
    identityCache_[identityKey] = fmuObj;
    return fmuObj;
}

namespace
{
class existing_directory_ro : public file_cache::directory_ro
{
public:
    explicit existing_directory_ro(cosim::filesystem::path path)
        : path_(std::move(path))
    { }

    [[nodiscard]] cosim::filesystem::path path() const override { return path_; }

private:
    cosim::filesystem::path path_;
};

// A private copy of an unpacked FMU with a normalized model description.
// The caller's directory is never modified.
class normalized_directory_ro : public file_cache::directory_ro
{
public:
    normalized_directory_ro(const cosim::filesystem::path& source,
        const std::string& modelDescription)
    {
        cosim::filesystem::copy(source, copy_.path(),
            cosim::filesystem::copy_options::recursive |
                cosim::filesystem::copy_options::copy_symlinks);
        write_binary_file(copy_.path() / "modelDescription.xml", modelDescription);
    }

    [[nodiscard]] cosim::filesystem::path path() const override
    {
        return copy_.path();
    }

private:
    cosim::utility::temp_dir copy_;
};
} // namespace

std::shared_ptr<fmu>
importer::import_unpacked(const cosim::filesystem::path& unpackedFMUPath)
{
    const import_guard guard(importInProgress_);
    prune_ptr_caches();
    const auto minModelDesc = peek_model_description(unpackedFMUPath);
    if (minModelDesc.fmiVersion == fmi_version::unknown) {
        throw error(make_error_code(errc::unsupported_feature),
            "Unsupported FMI version for FMU at '" +
                unpackedFMUPath.string() + "'");
    }
    const auto identityKey =
        minModelDesc.fmiVersion == fmi_version::v3_0
        ? "unpacked-v3:" +
            cosim::filesystem::canonical(unpackedFMUPath).u8string()
        : identity_cache_key(minModelDesc);
    auto const git = identityCache_.find(identityKey);
    if (git != end(identityCache_)) {
        return git->second.lock();
    }

    std::unique_ptr<file_cache::directory_ro> unpackedFMUDir;
    if (minModelDesc.normalizedModelDescription) {
        unpackedFMUDir = std::make_unique<normalized_directory_ro>(
            unpackedFMUPath, *minModelDesc.normalizedModelDescription);
    } else {
        unpackedFMUDir = std::make_unique<existing_directory_ro>(unpackedFMUPath);
    }
    std::shared_ptr<fmu> fmuObj;
    if (minModelDesc.fmiVersion == fmi_version::v1_0) {
        fmuObj = std::shared_ptr<fmu>(
            new v1::fmu(shared_from_this(), std::move(unpackedFMUDir)));
    } else if (minModelDesc.fmiVersion == fmi_version::v2_0) {
        fmuObj = std::shared_ptr<fmu>(
            new v2::fmu(shared_from_this(), std::move(unpackedFMUDir)));
    } else {
        fmuObj = std::shared_ptr<fmu>(
            new v3::fmu(shared_from_this(), std::move(unpackedFMUDir)));
    }
    identityCache_[identityKey] = fmuObj;
    return fmuObj;
}

std::string importer::last_error_message()
{
    return {jm_get_last_error(callbacks_.get())};
}

fmi_import_context_t* importer::fmilib_handle() const { return handle_.get(); }

void importer::prune_ptr_caches()
{
    for (auto it = begin(pathCache_); it != end(pathCache_);) {
        if (it->second.expired()) {
            pathCache_.erase(it++);
        } else {
            ++it;
        }
    }
    for (auto it = begin(identityCache_); it != end(identityCache_);) {
        if (it->second.expired()) {
            identityCache_.erase(it++);
        } else {
            ++it;
        }
    }
}

} // namespace cosim::fmi
