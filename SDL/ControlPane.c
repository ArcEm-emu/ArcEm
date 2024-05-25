/* Redraw and other services for the control pane */
/* (c) David Alan Gilbert 1995 - see Readme file for copying info */


#include "../armdefs.h"
#include "archio.h"
#include "armarc.h"
#include "ControlPane.h"

#include <stdarg.h>
#include <stdio.h>

#include <SDL.h>

void ControlPane_Init(ARMul_State *state)
{

}

void ControlPane_Error(int code,const char *fmt,...)
{
#if SDL_VERSION_ATLEAST(2, 0, 0)
  va_list args;
  char err[100];

  va_start(args,fmt);
  vsnprintf(err, sizeof(err), fmt, args);
  va_end(args);

  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ArcEm", err, NULL);
  fputs(err, stderr);
#else
  va_list args;
  va_start(args,fmt);
  /* Log it */
  vfprintf(stderr,fmt,args);
#endif
  /* Quit */
  exit(code);
}
