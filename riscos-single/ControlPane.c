/* Redraw and other services for the control pane */
/* (c) David Alan Gilbert 1995 - see Readme file for copying info */


#include "../armdefs.h"
#include "archio.h"
#include "armarc.h"
#include "ControlPane.h"
#include "arch/dbugsys.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include "kernel.h"
#include "swis.h"

void ControlPane_Init(ARMul_State *state)
{

}

void ControlPane_Error(int code,const char *fmt,...)
{
  char buf[1024];
  va_list args;
  /* Assume mode 28 is available */
  _swix(OS_ScreenMode,_INR(0,1),0,28);
  va_start(args,fmt);
  vsnprintf(buf,sizeof(buf),fmt,args);
  va_end(args);
  /* Log it */
  log_msgv(LOG_ERROR,"%s",buf);
  /* Report error */
  puts(buf);
  /* Quit */
  exit(code);
}

void log_msgv(int type, const char *format, va_list ap)
{
  /* stdout is reserved for menu/stats display on RISC OS */
  vfprintf(stderr, format, ap);
}
