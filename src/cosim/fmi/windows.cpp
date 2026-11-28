/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/fmi/windows.hpp"
#ifdef _WIN32

#    include <Windows.h> // IWYU pragma: keep
#    include <array>
#    include <cassert>
#    include <cstddef>
#    include <filesystem>
#    include <mutex>


namespace cosim::fmi
{

namespace
{
// Maximum size of environment variables
constexpr std::size_t max_env_var_size = 32767;

// Mutex to protect against concurrent access to PATH
std::mutex path_env_var_mutex; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): PATH changes must be synchronized across instances.
} // namespace


// Windows SDK declarations are provided through Windows.h.
// NOLINTBEGIN(misc-include-cleaner)
detail::additional_path::additional_path(const cosim::filesystem::path& path)
{
    std::scoped_lock const lock(path_env_var_mutex);

    std::array<WCHAR, max_env_var_size> currentPathZ{};
    const auto currentPathLen =
        GetEnvironmentVariableW(L"PATH", currentPathZ.data(), max_env_var_size);
    const auto currentPath = std::wstring(currentPathZ.data(), currentPathLen);

    if (currentPathLen > 0) {
        addedPath_ = L";";
    }
    addedPath_ += path.wstring();

    const auto newPath = currentPath + addedPath_;
    if (SetEnvironmentVariableW(L"PATH", newPath.c_str()) == 0) {
        assert(!"Failed to modify PATH environment variable");
    }
}


detail::additional_path::~additional_path() // NOLINT(bugprone-exception-escape): Preserve the existing noexcept destructor contract.
{
    std::scoped_lock const lock(path_env_var_mutex);

    std::array<WCHAR, max_env_var_size> currentPathZ{};
    const auto currentPathLen =
        GetEnvironmentVariableW(L"PATH", currentPathZ.data(), max_env_var_size);
    const auto currentPath = std::wstring(currentPathZ.data(), currentPathLen);

    const auto pos = currentPath.find(addedPath_);
    if (pos < std::wstring::npos) {
        const auto newPath = currentPath.substr(0, pos) + currentPath.substr(pos + addedPath_.size());
        if (SetEnvironmentVariableW(L"PATH", newPath.c_str()) == 0) {
            assert(!"Failed to reset PATH environment variable");
        }
        // NOLINTEND(misc-include-cleaner)
    }
}


cosim::filesystem::path fmu_binaries_dir(const cosim::filesystem::path& baseDir)
{
#    ifdef _WIN64
    const auto* const platformSubdir = L"win64";
#    else
    const auto platformSubdir = L"win32";
#    endif // _WIN64
    return baseDir / L"binaries" / platformSubdir;
}


} // namespace cosim::fmi

#endif // _WIN32
