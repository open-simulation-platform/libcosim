/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/fmi/windows.hpp"
#ifdef _WIN32

#    include <Windows.h>
#    include <cassert>
#    include <cstddef>
#    include <mutex>


namespace cosim
{
namespace fmi
{

namespace
{
// Maximum size of environment variables
constexpr std::size_t max_env_var_size = 32767;

// Mutex to protect against concurrent access to PATH
std::mutex path_env_var_mutex;
} // namespace


detail::additional_path::additional_path(const boost::filesystem::path& p)
{
    std::lock_guard<std::mutex> lock(path_env_var_mutex);

    WCHAR currentPathZ[max_env_var_size];
    const auto currentPathLen =
        GetEnvironmentVariableW(L"PATH", currentPathZ, max_env_var_size);
    const auto currentPath = std::wstring(currentPathZ, currentPathLen);

    if (currentPathLen > 0) {
        addedPath_ = L";";
    }
    addedPath_ += p.wstring();

    const auto newPath = currentPath + addedPath_;
    if (!SetEnvironmentVariableW(L"PATH", newPath.c_str())) {
        assert(!"Failed to modify PATH environment variable");
    }
}


detail::additional_path::~additional_path()
{
    std::lock_guard<std::mutex> lock(path_env_var_mutex);

    WCHAR currentPathZ[max_env_var_size];
    const auto currentPathLen =
        GetEnvironmentVariableW(L"PATH", currentPathZ, max_env_var_size);
    const auto currentPath = std::wstring(currentPathZ, currentPathLen);

    const auto pos = currentPath.find(addedPath_);
    if (pos < std::wstring::npos) {
        auto newPath = currentPath.substr(0, pos) + currentPath.substr(pos + addedPath_.size());
        if (!SetEnvironmentVariableW(L"PATH", newPath.c_str())) {
            assert(!"Failed to reset PATH environment variable");
        }
    }
}


boost::filesystem::path fmu_binaries_dir(const boost::filesystem::path& baseDir)
{
#    ifdef _WIN64
    const auto platformSubdir = L"win64";
#    else
    const auto platformSubdir = L"win32";
#    endif // _WIN64
    return baseDir / L"binaries" / platformSubdir;
}


} // namespace fmi
} // namespace cosim
#endif // _WIN32
