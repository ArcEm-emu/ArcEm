/* Redraw and other services for the control pane */
/* (c) David Alan Gilbert 1995 - see Readme file for copying info */


#include "../armdefs.h"
#include "archio.h"
#include "armarc.h"
#include "DispKbd.h"
#include "ControlPane.h"

#define CTRLPANEWIDTH 640
#define CTRLPANEHEIGHT 100

#define LEDHEIGHT 15
#define LEDWIDTH 15
#define LEDTOPS 50

/* HOSTDISPLAY is too verbose here - but HD could be hard disc somewhere else! */
#define HD HOSTDISPLAY
#define DC DISPLAYCONTROL


void ControlPane_Init(ARMul_State *state)
{

}

void ControlPane_RedrawLeds(ARMul_State *state)
{

}
