/* (c) David Alan Gilbert 1995 - see Readme file for copying info */
#ifndef CONTROLPANE_HEADER
#define CONTROLPANE_HEADER

/*----------------------------------------------------------------------------*/
void ControlPane_Init(ARMul_State *state);

/*----------------------------------------------------------------------------*/
void ControlPane_RedrawLeds(ARMul_State *state);

#ifdef SYSTEM_X 
/*----------------------------------------------------------------------------*/
void ControlPane_Event(ARMul_State *state,XEvent *e);
#endif

#endif
