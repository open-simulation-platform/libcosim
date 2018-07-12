/**
 *  \file
 *  C library header.
 *  \ingroup csecorec
 */
#ifndef CSE_H
#define CSE_H

#include <stddef.h>


/// \defgroup csecorec C library

#ifdef __cplusplus
extern "C" {
#endif



/**
 *  Writes the string "Hello World!" to a character buffer.
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
int cse_hello_world(char* buffer, size_t size);



#ifdef __cplusplus
} // extern(C)
#endif

#endif // header guard
