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

static void ami_easyrequest(const char *err)
{
	struct EasyStruct es;
	es.es_StructSize = sizeof(struct EasyStruct);
	es.es_Flags = 0;
	es.es_Title =  "ArcEm";
	es.es_TextFormat = err;
	es.es_GadgetFormat = "OK";

	if(IntuitionBase != NULL) {
		EasyRequestArgs(NULL, &es, NULL, NULL);
	} else {
		log_msg(LOG_INFO, "%s\n", err);
	}
}

void ControlPane_Error(bool fatal,const char *fmt,...)
{
  char err[100];
  va_list args;
  va_start(args,fmt);
  /* Log it */
  vsnprintf(err, sizeof(err), fmt, args);
  va_end(args);
  ami_easyrequest(err);
  
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
