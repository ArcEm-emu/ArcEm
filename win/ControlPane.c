/* Redraw and other services for the control pane */
/* (c) David Alan Gilbert 1995 - see Readme file for copying info */


#include "../armdefs.h"
#include "archio.h"
#include "armarc.h"
#include "ControlPane.h"
#include "arch/dbugsys.h"

#include <stdarg.h>
#include <stdio.h>

#include <windows.h>

void ControlPane_Init(ARMul_State *state)
{

}

void ControlPane_Error(int code,const char *fmt,...)
{
  char err[100];
  va_list args;

  /* Log it */
  va_start(args,fmt);
  log_msgv(LOG_ERROR,fmt,args);
  vsnprintf(err, sizeof(err), fmt, args);
  MessageBoxA(NULL, err, "ArcEm", MB_ICONERROR);
  va_end(args);

  /* Quit */
  exit(code);
}

void log_msgv(int type, const char *format, va_list ap)
{
  if (type >= LOG_WARN)
    vfprintf(stderr, format, ap);
  else
    vfprintf(stdout, format, ap);
}
