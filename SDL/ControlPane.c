/* Redraw and other services for the control pane */
/* (c) David Alan Gilbert 1995 - see Readme file for copying info */


#include "../armdefs.h"
#include "archio.h"
#include "armarc.h"
#include "ControlPane.h"
#include "arch/dbugsys.h"
#include "platform.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

bool ControlPane_Init(ARMul_State *state)
{
  return true;
}

void ControlPane_Error(bool fatal,const char *fmt,...)
{
#if SDL_VERSION_ATLEAST(2, 0, 0)
  va_list args;
  char err[100];

  va_start(args,fmt);
  SDL_vsnprintf(err, sizeof(err), fmt, args);
  va_end(args);

  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "ArcEm", err, NULL);
  log_msg(LOG_ERROR, "%s\n", err);
#else
  va_list args;

  va_start(args,fmt);
  log_msgv(LOG_ERROR,fmt,args);
  va_end(args);
  log_msg(LOG_ERROR,"\n");
#endif
  /* Quit */
  if (fatal)
    exit(EXIT_FAILURE);
}

void log_msgv(int type, const char *format, va_list ap)
{
#if SDL_VERSION_ATLEAST(2, 0, 0)
  switch (type) {
  case LOG_DBUG:
    SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_DEBUG, format, ap);
    break;
  case LOG_WARN:
    SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_WARN, format, ap);
    break;
  case LOG_ERROR:
    SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_ERROR, format, ap);
    break;
  default:
    SDL_LogMessageV(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_INFO, format, ap);
    break;
  }
#else
  if (type >= LOG_WARN)
    vfprintf(stderr, format, ap);
  else
    vfprintf(stdout, format, ap);
#endif
}
