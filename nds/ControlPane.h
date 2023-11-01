/* (c) David Alan Gilbert 1995 - see Readme file for copying info */
#ifndef CONTROLPANE_HEADER
#define CONTROLPANE_HEADER

void ControlPane_Init(ARMul_State *state);

/* Report an error and exit */
void ControlPane_Error(bool fatal,const char *fmt,...);

void OnKeyPressed(int key);
void OnKeyReleased(int key);

#endif
