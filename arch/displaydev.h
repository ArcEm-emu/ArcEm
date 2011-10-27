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
extern int DisplayDev_UseUpdateFlags; /* Global flag for whether the current device is using ARMul_State.UpdateFlags */

extern int DisplayDev_AutoUpdateFlags; /* Automatically select whether to use UpdateFlags or not */

extern int DisplayDev_Set(ARMul_State *state,const DisplayDev *dev); /* Switch to indicated display device, returns nonzero on failure */

/* Host must provide this function to initialize the default display device */
extern int DisplayDev_Init(ARMul_State *state);

/* Calculate cursor position relative to the first display pixel */
extern void DisplayDev_GetCursorPos(ARMul_State *state,int *x,int *y);

extern unsigned long DisplayDev_GetVIDCClockIn(void); /* Get VIDC source clock rate (affected by IOEB CR) */

extern void DisplayDev_VSync(ARMul_State *state); /* Trigger VSync interrupt & update ARMul_EmuRate. Note: Manipulates event queue! */

/* General endian swapping/endian-aware memcpy functions */

#ifdef HOST_BIGENDIAN
static inline ARMword EndianSwap(const ARMword a)
{
  return (a>>24) | (a<<24) | ((a>>8) & 0xff00) | ((a<<8) & 0xff0000);
}

static inline void EndianWordCpy(ARMword *dest,const ARMword *src,size_t count)
{
  while(count)
  {
    *dest++ = EndianSwap(*src++);
    count--;
  }
}

/* src = little-endian emu memory, dest = big-endian host memory */
extern void ByteCopy(char *dest,const char *src,int size);

/* src = big-endian host memory, dest = little-endian emu memory */
extern void InvByteCopy(char *dest,const char *src,int size);
#else
#define EndianSwap(X) (X)
#define EndianWordCpy(A,B,C) memcpy(A,B,(C)<<2)
#define ByteCopy(A,B,C) memcpy(A,B,C)
#define InvByteCopy(A,B,C) memcpy(A,B,C)
#endif

/* Helper functions for display devices */

extern void BitCopy(ARMword *dest,int destalign,const ARMword *src,int srcalign,int count);

extern int GetExpandTableSize(unsigned int srcbpp,unsigned int factor);

extern void GenExpandTable(ARMword *dest,unsigned int srcbpp,unsigned int factor,unsigned int mul);

extern void BitCopyExpand(ARMword *dest,int destalign,const ARMword *src,int srcalign,int count,const ARMword *expandtable,unsigned int srcbpp,unsigned int factor);

#endif

