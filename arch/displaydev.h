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
  void (*VIDCPutVal)(ARMul_State *state,ARMword address, ARMword data,bool bNw); /* Call made by core to handle writing to VIDC registers */
  void (*DAGWrite)(ARMul_State *state,int reg,ARMword val); /* Call made by core when video DAG registers are updated. reg 0=Vinit, 1=Vstart, 2=Vend, 3=Cinit */
  void (*IOEBCRWrite)(ARMul_State *state,ARMword val); /* Call made by core when IOEB control register is updated */
} DisplayDev;

/* Raw VIDC registers */
struct Vidc_Regs {
  uint_least16_t Palette[16];
  uint_least16_t BorderCol;
  uint_least16_t CursorPalette[3];
  uint_least16_t Horiz_Cycle;
  uint_least16_t Horiz_SyncWidth;
  uint_least16_t Horiz_BorderStart;
  uint_least16_t Horiz_DisplayStart;
  uint_least16_t Horiz_DisplayEnd;
  uint_least16_t Horiz_BorderEnd;
  uint_least16_t Horiz_CursorStart;
  uint_least16_t Horiz_Interlace;
  uint_least16_t Vert_Cycle;
  uint_least16_t Vert_SyncWidth;
  uint_least16_t Vert_BorderStart;
  uint_least16_t Vert_DisplayStart;
  uint_least16_t Vert_DisplayEnd;
  uint_least16_t Vert_BorderEnd;
  uint_least16_t Vert_CursorStart;
  uint_least16_t Vert_CursorEnd;
  uint_least16_t ControlReg;
  uint8_t SoundFreq;
  uint8_t StereoImageReg[8];

  /* This avoids -Wcast-align warnings with Clang. */
  uint64_t align;
};

#define VIDC (*(state->Display))

extern const DisplayDev *DisplayDev_Current; /* Pointer to current display device */
extern bool DisplayDev_UseUpdateFlags; /* Global flag for whether the current device is using MEMC.UpdateFlags */

extern bool DisplayDev_AutoUpdateFlags; /* Automatically select whether to use UpdateFlags or not. If true, this causes DisplayDev_UseUpdateFlags and DisplayDev_FrameSkip to be updated automatically. */

extern int DisplayDev_FrameSkip; /* If DisplayDev_UseUpdateFlags is true, this provides a frameskip value used by the standard & palettised drivers. If DisplayDev_UseUpdateFlags is false, it acts as failsafe counter that forces an update when a certain number of frames have passed */

extern int DisplayDev_Set(ARMul_State *state,const DisplayDev *dev); /* Switch to indicated display device, returns nonzero on failure */

/* Host must provide this function to initialize the default display device */
extern int DisplayDev_Init(ARMul_State *state);

extern void DisplayDev_Shutdown(ARMul_State *state);

/* Calculate cursor position relative to the first display pixel */
extern void DisplayDev_GetCursorPos(ARMul_State *state,int *x,int *y);

extern uint32_t DisplayDev_GetVIDCClockIn(void); /* Get VIDC source clock rate (affected by IOEB CR) */

extern void DisplayDev_VSync(ARMul_State *state); /* Trigger VSync interrupt & update ARMul_EmuRate. Note: Manipulates event queue! */

/* General endian swapping/endian-aware memcpy functions */

#ifdef HOST_BIGENDIAN
static inline ARMword EndianSwap(const ARMword a)
{
  return (a>>24) | (a<<24) | ((a>>8) & 0xff00) | ((a<<8) & 0xff0000);
}

static inline void EndianWordCpy(ARMword *restrict dest,const ARMword *restrict src,size_t count)
{
  while(count)
  {
    *dest++ = EndianSwap(*src++);
    count--;
  }
}

/* src = little-endian emu memory, dest = big-endian host memory */
extern void ByteCopy(void *restrict dest,const void *restrict src,size_t size);

/* src = big-endian host memory, dest = little-endian emu memory */
extern void InvByteCopy(void *restrict dest,const void *restrict src,size_t size);
#else
#include <string.h>

#define EndianSwap(X) (X)
#define EndianWordCpy(A,B,C) memcpy(A,B,(C)<<2)
#define ByteCopy(A,B,C) memcpy(A,B,C)
#define InvByteCopy(A,B,C) memcpy(A,B,C)
#endif

/* Helper functions for display devices */

extern void BitCopy(ARMword *restrict dest,int destalign,const ARMword *restrict src,int srcalign,int count);

extern int GetExpandTableSize(unsigned int srcbpp,unsigned int factor);

extern void GenExpandTable(ARMword *restrict dest,unsigned int srcbpp,unsigned int factor,ARMword mul);

extern void BitCopyExpand(ARMword *restrict dest,int destalign,const ARMword *restrict src,int srcalign,int count,const ARMword *restrict expandtable,unsigned int srcbpp,unsigned int factor);

#endif

