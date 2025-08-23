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

bool ControlPane_Init(ARMul_State *state)
{
  return true;
}

void ControlPane_Error(bool fatal,const char *fmt,...)
{
  char err[100];
  va_list args;

  /* Log it */
  va_start(args,fmt);
  vsnprintf(err, sizeof(err), fmt, args);
  va_end(args);

  log_msg(LOG_ERROR,"%s\n",err);
  MessageBoxA(NULL, err, "ArcEm", MB_ICONERROR);

  /* Quit */
  if (fatal)
    exit(EXIT_FAILURE);
}

void log_msgv(int type, const char *format, va_list ap)
{
  if (type >= LOG_WARN)
    vfprintf(stderr, format, ap);
  else
    vfprintf(stdout, format, ap);
}
