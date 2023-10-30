/* Redraw and other services for the control pane */
/* (c) David Alan Gilbert 1995 - see Readme file for copying info */


#include "../armdefs.h"
#include "archio.h"
#include "armarc.h"
#include "ControlPane.h"

#include <stdarg.h>
#include <stdio.h>

void ControlPane_Init(ARMul_State *state)
{

}

void ControlPane_Error(int code,const char *fmt,...)
{
  va_list args;
  va_start(args,fmt);
  /* Log it */
  vfprintf(stderr,fmt,args);
  /* Quit */
  exit(code);
}
