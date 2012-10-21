/* Redraw and other services for the control pane */
/* (c) David Alan Gilbert 1995 - see Readme file for copying info */


#include "../armdefs.h"
#include "archio.h"
#include "armarc.h"
#include "ControlPane.h"

#include <stdarg.h>
#include <stdio.h>
#include "kernel.h"
#include "swis.h"

void ControlPane_Init(ARMul_State *state)
{

}

void ControlPane_Error(int code,const char *fmt,...)
{
  /* Assume mode 28 is available */
  _swix(OS_ScreenMode,_INR(0,1),0,28);
  char buf[1024];
  va_list args;
  va_start(args,fmt);
  vsnprintf(buf,sizeof(buf),fmt,args);
  /* Log it */
  fputs(buf,stderr);
  /* Report error */
  puts(buf);
  /* Quit */
  exit(code);
}
