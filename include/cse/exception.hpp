/**
 *  \file
 *  \brief Exceptions and error codes.
 */
#ifndef CSE_EXCEPTION_HPP
#define CSE_EXCEPTION_HPP

#include <system_error>


namespace cse
{


/// Error codes specific to this library.
enum class errc
{
    /// An input file is corrupted or invalid.
    bad_file,

    /// The requested feature (e.g. an FMI feature) is unsupported.
    unsupported_feature,

    /// Error loading dynamic library (e.g. model code)
    dl_load_error,

    /// The model reported an error
    model_error,

    /// ZIP file error
    zip_error
};


/// A category for errors specific to this library.
const std::error_category& error_category() noexcept;


/// Constructs a library-specific error code.
std::error_code make_error_code(errc e) noexcept;


/// Constructs a library-specific error condition.
std::error_condition make_error_condition(errc e) noexcept;


/// The base class for exceptions specific to this library.
class error : public std::runtime_error
{
public:
    /// Constructs an exception with the given error code.
    error(std::error_code ec)
        : std::runtime_error(ec.message())
        , code_(ec)
    { }

    /**
     *  \brief  Constructs an exception with the given error code and an
     *          additional error message.
     *
     *  The `what()` function is guaranteed to return a string which contains
     *  the text in `msg` in addition to the standard message associated with
     *  `ec`.
     */
    error(std::error_code ec, const std::string& msg)
        : std::runtime_error(ec.message() + ": " + msg)
        , code_(ec)
    { }

    /// Returns an error code.
    const std::error_code& code() const noexcept { return code_; }

private:
    std::error_code code_;
};


} // namespace


namespace std
{
    // Enable implicit conversions from errc to std::error_code and
    // std::error_condition.
    template<> struct is_error_code_enum<cse::errc> : public true_type { };
    template<> struct is_error_condition_enum<cse::errc> : public true_type { };
} 
#endif // header guard
