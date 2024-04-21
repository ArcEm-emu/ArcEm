#include "armdefs.h"

extern int rMouseX;
extern int rMouseY;
extern int rMouseWidth;
extern int rMouseHeight;

extern int createWindow(ARMul_State *state, int w, int h);
extern int createBitmaps(int hWidth, int hHeight, int hBpp, int hXScale);
extern void setPaletteColour(int i, uint8_t r, uint8_t g, uint8_t b);
extern void setCursorPaletteColour(int i, uint8_t r, uint8_t g, uint8_t b);
extern int updateDisplay(void);
extern int resizeWindow(int hWidth, int hHeight);

extern void ProcessKey(ARMul_State *state, int nVirtKey, int lKeyData, int nKeyStat);
extern void MouseMoved(ARMul_State *state, int nMouseX, int nMouseY);

extern void sound_exit(void);
