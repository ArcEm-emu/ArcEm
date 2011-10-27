/*
  arch/displaydev.c

  (c) 2011 Jeffrey Lee <me@phlamethrower.co.uk>

  Part of Arcem released under the GNU GPL, see file COPYING
  for details.

  Utility functions for bridging the gap between the host display code and the
  emulator core
*/
#ifndef DISPLAYDEV_H
#define DISPLAYDEV_H

typedef struct {
  int (*Init)(ARMul_State *state,const struct Vidc_Regs *Vidc); /* Initialise display device, return nonzero on failure */
  void (*Shutdown)(ARMul_State *state); /* Shutdown display device */
  void (*VIDCPutVal)(ARMul_State *state,ARMword address, ARMword data,int bNw); /* Call made by core to handle writing to VIDC registers */
  void (*DAGWrite)(ARMul_State *state,int reg,ARMword val); /* Call made by core when video DAG registers are updated. reg 0=Vinit, 1=Vstart, 2=Vend, 3=Cinit */
  void (*IOEBCRWrite)(ARMul_State *state,ARMword val); /* Call made by core when IOEB control register is updated */
} DisplayDev;

/* Raw VIDC registers */
struct Vidc_Regs {
  unsigned int Palette[16];
  unsigned int BorderCol;
  unsigned int CursorPalette[3];
  unsigned int Horiz_Cycle;
  unsigned int Horiz_SyncWidth;
  unsigned int Horiz_BorderStart;
  unsigned int Horiz_DisplayStart;
  unsigned int Horiz_DisplayEnd;
  unsigned int Horiz_BorderEnd;
  unsigned int Horiz_CursorStart;
  unsigned int Horiz_Interlace;
  unsigned int Vert_Cycle;
  unsigned int Vert_SyncWidth;
  unsigned int Vert_BorderStart;
  unsigned int Vert_DisplayStart;
  unsigned int Vert_DisplayEnd;
  unsigned int Vert_BorderEnd;
  unsigned int Vert_CursorStart;
  unsigned int Vert_CursorEnd;
  unsigned int ControlReg;
  unsigned char SoundFreq;
  unsigned char StereoImageReg[8];
};

#define VIDC (*(state->Display))

extern const DisplayDev *DisplayDev_Current; /* Pointer to current display device */

extern int DisplayDev_Set(ARMul_State *state,const DisplayDev *dev); /* Switch to indicated display device, returns nonzero on failure */

/* Host must provide this function to initialize the default display device */
extern int DisplayDev_Init(ARMul_State *state);

/* Calculate cursor position relative to the first display pixel */
extern void DisplayDev_GetCursorPos(ARMul_State *state,int *x,int *y);

extern unsigned long DisplayDev_GetVIDCClockIn(void); /* Get VIDC source clock rate (affected by IOEB CR) */

#endif

