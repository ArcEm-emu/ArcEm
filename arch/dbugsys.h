/**
  dbugsys.h
  
  Handle calls to debug Arcem, these macros provide the code needed
  to make sure the debugging calls disapear completely in a non
  debug build
 */

#ifndef _DBUGSYS_H_
#define _DBUGSYS_H_

#include <stdio.h>

#undef IOC_TRACE
#undef DEBUG
#undef DEBUG_MEMC
#undef DEBUG_I2C
#undef DEBUG_DATA
#undef DEBUG_DMAWRITE
#undef DEBUG_DMAREAD
#undef DEBUG_INTS

/* This function has no coresponding body, the compiler
  is clever enough to use it to swallow the arguments to 
  debugging calls */
int dbug_null(const char *format, ...);

#ifdef IOC_TRACE
# define dbug_ioc printf
#else 
 /* Make the debugging statements disapear in a non IOC_TRACE build */
# define dbug_ioc 1?(void)0:(void)dbug_null
#endif

#ifdef DEBUG
# define dbug printf
#else
# define dbug 1?(void)0:(void)dbug_null
#endif

#ifdef DEBUG_MEMC
# define dbug_memc printf
#else
# define dbug_memc 1?(void)0:(void)dbug_null
#endif

#ifdef DEBUG_I2C
# define dbug_i2c printf
#else
# define dbug_i2c 1?(void)0:(void)dbug_null
#endif

#ifdef DEBUG_DATA
# define dbug_data printf
#else
# define dbug_data 1?(void)0:(void)dbug_null
#endif

#ifdef DEBUG_DMAWRITE
# define dbug_dmawrite printf
#else
# define dbug_dmawrite 1?(void)0:(void)dbug_null
#endif

#ifdef DEBUG_DMAREAD
# define dbug_dmaread printf
#else
# define dbug_dmaread 1?(void)0:(void)dbug_null
#endif

#ifdef DEBUG_INTS
# define dbug_ints printf
#else
# define dbug_ints 1?(void)0:(void)dbug_null
#endif

#endif /* _DBUGSYS_H_ */
