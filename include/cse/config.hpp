/**
 *  \file
 *  \brief Library-wide configuration macros.
 */
#ifndef CSE_CONFIG_HPP
#define CSE_CONFIG_HPP


// Handle DLL symbol visibility
#if defined(_WIN32) && !defined(CSECORE_STATIC)
#    if defined(CSECORE_DLL_EXPORT)
#        define CSE_EXPORT __declspec(dllexport)
#    else
#        define CSE_EXPORT __declspec(dllimport)
#    endif
#else
#    define CSE_EXPORT
#endif


/// \defgroup csecore CSE core C++ library

/**
 *  \brief Top-level CSE library namespace
 *  \ingroup csecore
 */
namespace cse { }


#endif // header guard
