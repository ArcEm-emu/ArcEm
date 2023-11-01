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
#undef DEBUG_HOSTFS

#ifdef SYSTEM_nds
#undef IOC_WARN
#undef WARN
#undef WARN_MEMC
#undef WARN_I2C
#undef WARN_DATA
#undef WARN_DMAWRITE
#undef WARN_DMAREAD
#undef WARN_INTS
#undef WARN_VIDC
#undef WARN_KEYBOARD
#undef WARN_FDC1772
#undef WARN_HDC63463
#undef WARN_CONFIG
#undef WARN_HOSTFS
#else
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
#define WARN_HOSTFS
#endif

#define LOG_DBUG 1
#define LOG_INFO 2
#define LOG_WARN 3
#define LOG_ERROR 4

extern void log_msgv(int type, const char *format, va_list ap);

static inline void
log_msg(int type, const char *format, ...)
{
  va_list ap;

  va_start(ap, format);
  log_msgv(type, format, ap);
  va_end(ap);
}

static inline void
log_dbug(const char *format, ...)
{
  va_list ap;

  va_start(ap, format);
  log_msgv(LOG_DBUG, format, ap);
  va_end(ap);
}

static inline void
log_warn(const char *format, ...)
{
  va_list ap;

  va_start(ap, format);
  log_msgv(LOG_WARN, format, ap);
  va_end(ap);
}

/* This function has no coresponding body, the compiler
  is clever enough to use it to swallow the arguments to 
  debugging calls */
int log_null(const char *format, ...);

#ifdef IOC_TRACE
# define dbug_ioc log_dbug
#else 
 /* Make the debugging statements disapear in a non IOC_TRACE build */
# define dbug_ioc 1?(void)0:(void)log_null
#endif

#ifdef IOC_WARN
# define warn_ioc log_warn
#else
 /* Make the warning statements disapear in a non IOC_WARN build */
# define warn_ioc 1?(void)0:(void)log_null
#endif

#ifdef DEBUG
# define dbug log_dbug
#else
# define dbug 1?(void)0:(void)log_null
#endif

#ifdef WARN
# define warn log_warn
#else
# define warn 1?(void)0:(void)log_null
#endif

#ifdef DEBUG_MEMC
# define dbug_memc log_dbug
#else
# define dbug_memc 1?(void)0:(void)log_null
#endif

#ifdef WARN_MEMC
# define warn_memc log_warn
#else
# define warn_memc 1?(void)0:(void)log_null
#endif

#ifdef DEBUG_I2C
# define dbug_i2c log_dbug
#else
# define dbug_i2c 1?(void)0:(void)log_null
#endif

#ifdef WARN_I2C
# define warn_i2c log_warn
#else
# define warn_i2c 1?(void)0:(void)log_null
#endif

#ifdef DEBUG_DATA
# define dbug_data log_dbug
#else
# define dbug_data 1?(void)0:(void)log_null
#endif

#ifdef WARN_DATA
# define warn_data log_warn
#else
# define warn_data 1?(void)0:(void)log_null
#endif

#ifdef DEBUG_DMAWRITE
# define dbug_dmawrite log_dbug
#else
# define dbug_dmawrite 1?(void)0:(void)log_null
#endif

#ifdef WARN_DMAWRITE
# define warn_dmawrite log_warn
#else
# define warn_dmawrite 1?(void)0:(void)log_null
#endif

#ifdef DEBUG_DMAREAD
# define dbug_dmaread log_dbug
#else
# define dbug_dmaread 1?(void)0:(void)log_null
#endif

#ifdef WARN_DMAREAD
# define warn_dmaread log_warn
#else
# define warn_dmaread 1?(void)0:(void)log_null
#endif

#ifdef DEBUG_INTS
# define dbug_ints log_dbug
#else
# define dbug_ints 1?(void)0:(void)log_null
#endif

#ifdef WARN_INTS
# define warn_ints log_warn
#else
# define warn_ints 1?(void)0:(void)log_null
#endif

#ifdef DEBUG_VIDC
# define dbug_vidc log_dbug
#else
# define dbug_vidc 1?(void)0:(void)log_null
#endif

#ifdef WARN_VIDC
# define warn_vidc log_warn
#else
# define warn_vidc 1?(void)0:(void)log_null
#endif

#ifdef DEBUG_KEYBOARD
# define dbug_kbd log_dbug
#else
# define dbug_kbd 1?(void)0:(void)log_null
#endif

#ifdef WARN_KEYBOARD
# define warn_kbd log_warn
#else
# define warn_kbd 1?(void)0:(void)log_null
#endif

#ifdef DEBUG_FDC1772
# define dbug_fdc log_dbug
#else
# define dbug_fdc 1?(void)0:(void)log_null
#endif

#ifdef WARN_FDC1772
# define warn_fdc log_warn
#else
# define warn_fdc 1?(void)0:(void)log_null
#endif

#ifdef DEBUG_HDC63463
# define dbug_hdc log_dbug
#else
# define dbug_hdc 1?(void)0:(void)log_null
#endif

#ifdef WARN_HDC63463
# define warn_hdc log_warn
#else
# define warn_hdc 1?(void)0:(void)log_null
#endif

#ifdef DEBUG_CONFIG
# define dbug_cfg log_dbug
#else
# define dbug_cfg 1?(void)0:(void)log_null
#endif

#ifdef WARN_CONFIG
# define warn_cfg log_warn
#else
# define warn_cfg 1?(void)0:(void)log_null
#endif

#ifdef DEBUG_HOSTFS
# define dbug_hostfs log_dbug
#else
# define dbug_hostfs 1?(void)0:(void)log_null
#endif

#ifdef WARN_HOSTFS
# define warn_hostfs log_warn
#else
# define warn_hostfs 1?(void)0:(void)log_null
#endif

#endif /* _DBUGSYS_H_ */
