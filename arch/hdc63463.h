/* The hard drive controller as used in the A4x0 and on the podule for
   A3x0's.

   (C) David Alan Gilbert 1995
*/

#ifndef HDC63463_HEADER
#define HDC63463_HEADER

#include "../armdefs.h"
#include "../armopts.h"

struct HDCReadDataStr {
  unsigned char US,PHA,LCAH,LCAL,LHA,LSA,SCNTH,SCNTL;
  unsigned int BuffersLeft;
  unsigned int NextDestBuffer;
};

struct HDCWriteDataStr {
  unsigned char US,PHA,SCNTH,SCNTL,LSA,LHA,LCAL,LCAH;
  unsigned int BuffersLeft;
  unsigned int CurrentSourceBuffer;
};

struct HDCWriteFormatStr {
  unsigned char US,PHA,SCNTH,SCNTL;
  unsigned int SectorsLeft;
  unsigned int CurrentSourceBuffer;
};

union HDCCommandDataStr {
  struct HDCReadDataStr ReadData;
  struct HDCWriteFormatStr WriteFormat;
  struct HDCWriteDataStr WriteData;
};

struct HDCshape {
  unsigned int NCyls,NHeads,NSectors,RecordLength;
};

struct HDCStruct {
  FILE *HardFile[4];
  int LastCommand; /* -1=idle, 0xfff=command execution complete, other=command busy */
  unsigned char StatusReg;
  unsigned int Track[4];
  unsigned int Sector;
  int DREQ;
  int CurrentlyOpenDataBuffer;
  unsigned int DBufPtrs[2];
  unsigned char DBufs[2][256];
  unsigned int PBPtr;
  unsigned char ParamBlock[16];
  int HaveGotSpecify;    /* True once specify received */
  unsigned char SSB; /* Seems to hold error reports */

  int DelayCount;
  int DelayLatch;

  union HDCCommandDataStr CommandData;

  /* Stuff set in specify command */
  unsigned char dmaNpio;
  unsigned char CEDint; /* interrupt masks - >> 0 << when interrupt enabled */
  unsigned char SEDint;
  unsigned char DERint;
  unsigned char CUL; /* Connected unit list */
  struct HDCshape specshape;

  /* Actual size of the drive as set in the config file */
  struct HDCshape configshape[4];
};

/* Write to HDC memory space */
void HDC_Write(ARMul_State *state, int offset, int data);
/* Read from HDC memory space */
ARMword HDC_Read(ARMul_State *state, int offset);

void HDC_Init(ARMul_State *state);

unsigned int HDC_Regular(ARMul_State *state);

#define HDC (PRIVD->HDCData)

#endif
