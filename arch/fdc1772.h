/* Emulation of 1772 fdc - (C) David Alan Gilbert 1995 */
/* (c) David Alan Gilbert 1995 - see Readme file for copying info */
#ifndef FDC1772
#define FDC1772

#include "../armdefs.h"
#include "../armopts.h"

struct FDCStruct{
  FILE *FloppyFile[4];
  unsigned char LastCommand;
  int Direction; /* -1 or 1 */
  unsigned char StatusReg;
  unsigned char Track;
  unsigned char Sector;
  unsigned char Sector_ReadAddr; /* Used to hold the next sector to be found! */
  unsigned char Data;
  int BytesToGo;
  int DelayCount;
  int DelayLatch;
  int CurrentDisc;
#ifdef MACOSX
  char* driveFiles[4];  // In MAE, we use *real* filenames for disks
#endif
};

ARMword FDC_Read(ARMul_State *state, ARMword offset);

ARMword FDC_Write(ARMul_State *state, ARMword offset, ARMword data, int bNw);

void FDC_LatchAChange(ARMul_State *state);
void FDC_LatchBChange(ARMul_State *state);

void FDC_Init(ARMul_State *state);

void FDC_ReOpen(ARMul_State *state,int drive);

unsigned FDC_Regular(ARMul_State *state);

#define FDC (PRIVD->FDCData)

#endif
