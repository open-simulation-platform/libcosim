/**
 *  \file
 *  \brief C library header.
 *  \ingroup csecorec
 */
#ifndef CSE_H
#define CSE_H

#include <stddef.h>


/// \defgroup csecorec C library

// Handle DLL symbol visibility
#if defined(_WIN32) && !defined(CSECOREC_STATIC)
#    if defined(CSECOREC_DLL_EXPORT)
#        define CSE_C_EXPORT __declspec(dllexport)
#    else
#        define CSE_C_EXPORT __declspec(dllimport)
#    endif
#else
#    define CSE_C_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif



/**
 *  \brief  Writes the string "Hello World!" to a character buffer.
 *
 *  The size of `buffer` must be at least `size`. If it is less than 13 bytes,
 *  the string will be truncated.  The string will always be terminated by a
 *  zero byte, even if it is truncated.
 *
 *  \param buffer
 *      A buffer to hold the friendly greeting.
 *  \param size
 *      The size of the buffer.
 *
 *  \returns
 *      The answer to the ultimate question about life, the universe and
 *      everything, for no apparent reason.
 */
CSE_C_EXPORT int cse_hello_world(char* buffer, size_t size);



#ifdef __cplusplus
} // extern(C)
#endif

#endif // header guard
