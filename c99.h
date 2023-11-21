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

#include <stdint.h>

#ifdef _MSC_VER
#define inline __inline
typedef unsigned char bool;
#define true 1
#define false 0
#else
#include <stdbool.h>
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
