/**
  dbugsys.h
  
  Handle calls to debug Arcem, these macros provide the code needed
  to make sure the debugging calls disapear completely in a non
  debug build
 */

#ifndef _DBUGSYS_H_
#define _DBUGSYS_H_

#include <stdio.h>
#include <stdarg.h>

#undef IOC_TRACE
#undef DEBUG
#undef DEBUG_MEMC
#undef DEBUG_I2C
#undef DEBUG_DATA
#undef DEBUG_DMAWRITE
#undef DEBUG_DMAREAD
#undef DEBUG_INTS
#undef DEBUG_VIDC
#undef DEBUG_KEYBOARD
#undef DEBUG_FDC1772
#undef DEBUG_HDC63463
#undef DEBUG_CONFIG

#define IOC_WARN
#define WARN
#define WARN_MEMC
#define WARN_I2C
#define WARN_DATA
#define WARN_DMAWRITE
#define WARN_DMAREAD
#define WARN_INTS
#define WARN_VIDC
#define WARN_KEYBOARD
#define WARN_FDC1772
#define WARN_HDC63463
#define WARN_CONFIG

#define dbug_stdout printf

static inline void
dbug_stderr(const char *format, ...)
{
  va_list ap;

  va_start(ap, format);
  vfprintf(stderr, format, ap);
  va_end(ap);
}

/* This function has no coresponding body, the compiler
  is clever enough to use it to swallow the arguments to 
  debugging calls */
int dbug_null(const char *format, ...);

#ifdef IOC_TRACE
# define dbug_ioc dbug_stdout
#else 
 /* Make the debugging statements disapear in a non IOC_TRACE build */
# define dbug_ioc 1?(void)0:(void)dbug_null
#endif

#ifdef IOC_WARN
# define warn_ioc dbug_stderr
#else
 /* Make the warning statements disapear in a non IOC_WARN build */
# define warn_ioc 1?(void)0:(void)dbug_null
#endif

#ifdef DEBUG
# define dbug dbug_stdout
#else
# define dbug 1?(void)0:(void)dbug_null
#endif

#ifdef WARN
# define warn dbug_stderr
#else
# define warn 1?(void)0:(void)dbug_null
#endif

#ifdef DEBUG_MEMC
# define dbug_memc dbug_stdout
#else
# define dbug_memc 1?(void)0:(void)dbug_null
#endif

#ifdef WARN_MEMC
# define warn_memc dbug_stderr
#else
# define warn_memc 1?(void)0:(void)dbug_null
#endif

#ifdef DEBUG_I2C
# define dbug_i2c dbug_stdout
#else
# define dbug_i2c 1?(void)0:(void)dbug_null
#endif

#ifdef WARN_I2C
# define warn_i2c dbug_stderr
#else
# define warn_i2c 1?(void)0:(void)dbug_null
#endif

#ifdef DEBUG_DATA
# define dbug_data dbug_stdout
#else
# define dbug_data 1?(void)0:(void)dbug_null
#endif

#ifdef WARN_DATA
# define warn_data dbug_stderr
#else
# define warn_data 1?(void)0:(void)dbug_null
#endif

#ifdef DEBUG_DMAWRITE
# define dbug_dmawrite dbug_stdout
#else
# define dbug_dmawrite 1?(void)0:(void)dbug_null
#endif

#ifdef WARN_DMAWRITE
# define warn_dmawrite dbug_stderr
#else
# define warn_dmawrite 1?(void)0:(void)dbug_null
#endif

#ifdef DEBUG_DMAREAD
# define dbug_dmaread dbug_stdout
#else
# define dbug_dmaread 1?(void)0:(void)dbug_null
#endif

#ifdef WARN_DMAREAD
# define warn_dmaread dbug_stderr
#else
# define warn_dmaread 1?(void)0:(void)dbug_null
#endif

#ifdef DEBUG_INTS
# define dbug_ints dbug_stdout
#else
# define dbug_ints 1?(void)0:(void)dbug_null
#endif

#ifdef WARN_INTS
# define warn_ints dbug_stderr
#else
# define warn_ints 1?(void)0:(void)dbug_null
#endif

#ifdef DEBUG_VIDC
# define dbug_vidc dbug_stdout
#else
# define dbug_vidc 1?(void)0:(void)dbug_null
#endif

#ifdef WARN_VIDC
# define warn_vidc dbug_stderr
#else
# define warn_vidc 1?(void)0:(void)dbug_null
#endif

#ifdef DEBUG_KEYBOARD
# define dbug_kbd dbug_stdout
#else
# define dbug_kbd 1?(void)0:(void)dbug_null
#endif

#ifdef WARN_KEYBOARD
# define warn_kbd dbug_stderr
#else
# define warn_kbd 1?(void)0:(void)dbug_null
#endif

#ifdef DEBUG_FDC1772
# define dbug_fdc dbug_stdout
#else
# define dbug_fdc 1?(void)0:(void)dbug_null
#endif

#ifdef WARN_FDC1772
# define warn_fdc dbug_stderr
#else
# define warn_fdc 1?(void)0:(void)dbug_null
#endif

#ifdef DEBUG_HDC63463
# define dbug_hdc dbug_stdout
#else
# define dbug_hdc 1?(void)0:(void)dbug_null
#endif

#ifdef WARN_HDC63463
# define warn_hdc dbug_stderr
#else
# define warn_hdc 1?(void)0:(void)dbug_null
#endif

#ifdef DEBUG_CONFIG
# define dbug_cfg dbug_stdout
#else
# define dbug_cfg 1?(void)0:(void)dbug_null
#endif

#ifdef WARN_CONFIG
# define warn_cfg dbug_stderr
#else
# define warn_cfg 1?(void)0:(void)dbug_null
#endif

#endif /* _DBUGSYS_H_ */
