/*
  c99.h

  (c) 2011 Jeffrey Lee <me@phlamethrower.co.uk>

  Common header for C99-related things, and workarounds for non-C99 compilers

  Part of Arcem released under the GNU GPL, see file COPYING
  for details.

*/

#ifndef C99_H
#define C99_H

/* For now, assume we're using a C99 compiler */

#include <stddef.h>

#ifdef __GNUC__
#define GCC_VERSION (__GNUC__ * 10000 \
                   + __GNUC_MINOR__ * 100 \
                   + __GNUC_PATCHLEVEL__)
#else
#define GCC_VERSION 0
#endif


#if defined(_MSC_VER) && (_MSC_VER < 1600)
typedef __int8 int8_t;
typedef __int16 int16_t;
typedef __int32 int32_t;
typedef __int64 int64_t;
typedef unsigned __int8 uint8_t;
typedef unsigned __int16 uint16_t;
typedef unsigned __int32 uint32_t;
typedef unsigned __int64 uint64_t;

typedef int8_t int_fast8_t;
typedef int32_t int_fast16_t;
typedef uint8_t uint_fast8_t;
typedef uint32_t uint_fast16_t;

typedef int8_t int_least8_t;
typedef int16_t int_least16_t;
typedef uint8_t uint_least8_t;
typedef uint16_t uint_least16_t;

#define UINT32_C(x) (x ## U)
#define UINT64_C(x) (x ## ULL)

#define INT32_MIN (-2147483647i32 - 1)
#define INT32_MAX 2147483647i32
#define UINT32_MAX 0xffffffffui32
#else
#include <stdint.h>
#endif

#if defined(_MSC_VER) && (_MSC_VER < 1800)
#define PRIuFAST16 PRIu32
#define PRIxFAST16 PRIx32
#define PRId32 "I32d"
#define PRIu32 "I32u"
#define PRIx32 "I32x"
#define SCNx32 "lx"
#else
#include <inttypes.h>
#endif

#ifdef _WIN32
#define PRIuSIZE "Iu"
#define PRIxSIZE "Ix"
#else
#define PRIuSIZE "zu"
#define PRIxSIZE "zx"
#endif

#if defined(_MSC_VER) && (_MSC_VER < 1800)
typedef unsigned char bool;
#define true 1
#define false 0
#else
#include <stdbool.h>
#endif

#ifdef _MSC_VER
#define inline __inline
#endif

#if __STDC_VERSION__ >= 199901L
#define RESTRICT restrict
#elif GCC_VERSION >= 30100
#define RESTRICT __restrict__
#elif (defined(_MSC_VER) && _MSC_VER >= 1400) || defined(__WATCOMC__)
#define RESTRICT __restrict
#else
#define RESTRICT
#endif

#if __STDC_VERSION__ >= 199901L
/* __func__ is available */
#elif defined(__func__)
/* __func__ is available */
#else
#define __func__ __FUNCTION__
#endif

#if GCC_VERSION >= 30000
/* GCC 2 does not recognise %z format specifiers */
#define GNU_PRINTF(x, y) __attribute__((__format__(__printf__, x, y)))
#define MSVC_PRINTF
#elif defined(_MSC_VER) && _MSC_VER > 1400
#define GNU_PRINTF(x, y)
#define MSVC_PRINTF _Printf_format_string_
#else
#define GNU_PRINTF(x, y)
#define MSVC_PRINTF
#endif

#ifdef SYSTEM_nds
/* Use integer only versions of printf and scanf to reduce the executable size */
#include <stdio.h>

#define printf iprintf
#define fprintf fiprintf
#define sprintf siprintf
#define snprintf sniprintf

#define vprintf viprintf
#define vfprintf vfiprintf
#define vsprintf vsiprintf
#define vsnprintf vsniprintf

#define sscanf siscanf
#define fscanf fiscanf
#endif

#endif
