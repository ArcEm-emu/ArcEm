/* Redraw and other services for the control pane */
/* (c) David Alan Gilbert 1995 - see Readme file for copying info */


#include "../armdefs.h"
#include "../arch/archio.h"
#include "../arch/armarc.h"
#include "../arch/ControlPane.h"
#include "../arch/dbugsys.h"

#include <Cocoa/Cocoa.h>
#include <dispatch/dispatch.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

bool ControlPane_Init(ARMul_State *state)
{
  UNUSED_VAR(state);
  return true;
}

void ControlPane_Error(bool fatal,const char *fmt,...)
{
  const size_t errlen = 100;
  char *err = malloc(errlen);
  va_list args;

  /* Log it */
  va_start(args,fmt);
  vsnprintf(err,errlen,fmt,args);
  va_end(args);

  log_msg(LOG_ERROR,"%s\n",err);
  dispatch_sync(dispatch_get_main_queue(), ^{
      NSAlert *alert = [[NSAlert alloc] init];
      alert.alertStyle = NSAlertStyleCritical;
      alert.messageText = @"ArcEm";
      alert.informativeText = @(err);
      [alert runModal];
      free(err);
  });

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
