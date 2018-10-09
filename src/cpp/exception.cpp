#include <cse/exception.hpp>

#include "cse/error.hpp"
#include <cse/lib_info.hpp>


namespace cse
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
            case errc::bad_file:
                return "Bad file";
            case errc::unsupported_feature:
                return "Unsupported feature";
            case errc::dl_load_error:
                return "Error loading dynamic library";
            case errc::model_error:
                return "Model error";
            case errc::simulation_error:
                return "Simulation error";
            case errc::zip_error:
                return "ZIP file error";
            default:
                CSE_PANIC();
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


} // namespace cse
