/* (c) David Alan Gilbert 1995 - see Readme file for copying info */
#ifndef CONTROLPANE_HEADER
#define CONTROLPANE_HEADER

#ifdef SYSTEM_X 
#include "X11/Xlib.h"
#include "X11/Xutil.h"
#endif

void ControlPane_Init(ARMul_State *state);

#ifdef SYSTEM_X 
void ControlPane_Event(ARMul_State *state, XEvent *e);
#endif

/* Report an error and exit */
void ControlPane_Error(int code,const char *fmt,...);

#endif
