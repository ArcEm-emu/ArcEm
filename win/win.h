#include "armdefs.h"

extern int rMouseX;
extern int rMouseY;
extern int rMouseWidth;
extern int rMouseHeight;

extern int createWindow(int w, int h, int bpp);
extern int updateDisplay(void);
extern int resizeWindow(int hWidth, int hHeight);

extern void ProcessKey(ARMul_State *state, int nVirtKey, int lKeyData, int nKeyStat);
extern void MouseMoved(ARMul_State *state, int nMouseX, int nMouseY);

extern void sound_exit(void);
