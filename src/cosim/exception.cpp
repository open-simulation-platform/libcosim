/*
 *  This Source Code Form is subject to the terms of the Mozilla Public
 *  License, v. 2.0. If a copy of the MPL was not distributed with this
 *  file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */
#include "cosim/exception.hpp"

#include "cosim/error.hpp"
#include "cosim/lib_info.hpp"


namespace cosim
{


namespace
{
class my_error_category : public std::error_category
{
public:
    const char* name() const noexcept final override
    {
        return library_short_name;
    }

    std::string message(int ev) const final override
    {
        switch (static_cast<errc>(ev)) {
            case errc::success:
                return "Success";
            case errc::bad_file:
                return "Bad file";
            case errc::unsupported_feature:
                return "Unsupported feature";
            case errc::dl_load_error:
                return "Error loading dynamic library";
            case errc::model_error:
                return "Model error";
            case errc::nonfatal_bad_value:
                return "Variable value is invalid or out of range";
            case errc::simulation_error:
                return "Simulation error";
            case errc::invalid_system_structure:
                return "Invalid system structure";
            case errc::zip_error:
                return "ZIP file error";
            default:
                COSIM_PANIC();
        }
    }
};
} // namespace


const std::error_category& error_category() noexcept
{
    static my_error_category instance;
    return instance;
}


std::error_condition make_error_condition(errc e) noexcept
{
    return std::error_condition(static_cast<int>(e), error_category());
}


std::error_code make_error_code(errc e) noexcept
{
    return std::error_code(static_cast<int>(e), error_category());
}


} // namespace cosim
