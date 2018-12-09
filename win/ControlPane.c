/* Redraw and other services for the control pane */
/* (c) David Alan Gilbert 1995 - see Readme file for copying info */


#include "../armdefs.h"
#include "archio.h"
#include "armarc.h"
#include "ControlPane.h"

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
  va_start(args,fmt);
  /* Log it */
  vfprintf(stderr,fmt,args);
  vsnprintf(err, sizeof(err), fmt, args);
  MessageBoxA(NULL, err, "ArcEm", MB_ICONERROR);
  /* Quit */
  exit(code);
}
