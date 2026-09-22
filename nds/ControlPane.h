/* (c) David Alan Gilbert 1995 - see Readme file for copying info */
#ifndef CONTROLPANE_HEADER
#define CONTROLPANE_HEADER

void ControlPane_Init(ARMul_State *state);

/* Report an error and exit */
void ControlPane_Error(bool fatal,const char *fmt,...);

bool ControlPane_ProcessTouchPressed(ARMul_State *state, int px, int py);
bool ControlPane_ProcessTouchHeld(ARMul_State *state, int px, int py);
bool ControlPane_ProcessTouchReleased(ARMul_State *state);

void ControlPane_Redraw(void);

#endif
