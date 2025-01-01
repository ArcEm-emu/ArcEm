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
#define PRIx32 "I32x"
#define SCNx32 "lx"
#else
#include <inttypes.h>
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

#endif
