#ifndef ARMARC_HEADER
#define ARMARC_HEADER
/* (c) David Alan Gilbert 1995-1998 - see Readme file for copying info */

#include "../armdefs.h"
#include "../armopts.h"
#include "DispKbd.h"
#include "archio.h"
#include "fdc1772.h"
#include "hdc63463.h"
#ifdef SOUND_SUPPORT
#include "sound.h"
#endif

/* Memory map locations */
#define MEMORY_0x3800000_R_ROM_HIGH   0x3800000  /* Some sections of the memory map    */
#define MEMORY_0x3800000_W_LOG2PHYS   0x3800000  /* have different functions when read */
#define MEMORY_0x3400000_R_ROM_LOW    0x3400000  /* or written to.                     */
#define MEMORY_0x3600000_W_DMA_GEN    0x3600000
#define MEMORY_0x3400000_W_VIDEOCON   0x3400000

#define MEMORY_0x3000000_CON_IO       0x3000000

#define MEMORY_0x2000000_RAM_PHYS     0x2000000


/* Erm - this has to be 256....*/
#define UPDATEBLOCKSIZE 256

typedef void (*ARMWriteFunc)(ARMul_State* state, ARMword addr, ARMword data, int bNw);
typedef ARMword (*ARMReadFunc)(ARMul_State* state, ARMword addr);
typedef void (*ARMEmuFunc)(ARMword instr, ARMword pc);

ARMword GetWord(ARMword address);
void PutVal(ARMul_State *state, ARMword address, ARMword data, int bNw,
            int Statechange, ARMEmuFunc newfunc);

#define PutWord(state, address, data) \
        PutVal(state, address, data, 0, 1, ARMul_Emulate_DecodeInstr)


void ARMul_Emulate_DecodeInstr(ARMword instr, ARMword pc);

typedef struct {
  ARMWriteFunc writeFunc; /* NULL means definitely abort */
  ARMReadFunc readFunc;   /* NULL means definitely abort */
  ARMword offset;         /* Offset used by the read and write funcs */
} pageDesc;               /* Description for one 4K page */

struct MEMCStruct {
  ARMword *ROMHigh;           /* ROM high and low are to seperate rom areas */
  ARMword ROMHighSize;        /* For mapping in ROMs of different types eg. */
  ARMword *ROMLow;            /* 8bit Rom and 32bit Rom, or access speeds */ 
  ARMword ROMLowSize;
  ARMword *PhysRam;
  unsigned int RAMSize;

  int PageTable[512]; /* Good old fashioned MEMC1 page table */
  ARMword ControlReg;
  ARMword PageSizeFlags;
  ARMword Vinit,Vstart,Vend,Cinit;

  /* Sound buffer addresses */
  ARMword Sstart; /* The start and end of the next buffer */
  ARMword SendN;
  ARMword Sptr; /* Current position in current buffer */
  ARMword SendC; /* End of current buffer */
  ARMword SstartC; /* The start of the current buffer */
  int NextSoundBufferValid; /* This indicates whether the next buffer has been set yet */

  int ROMMapFlag; /* 0 means ROM is mapped as normal,1 means
                     processor hasn't done access to low memory, 2 means it
                     hasn't done an access to ROM space, so in 1 & 2 accesses
                     to  low mem access ROM */

  unsigned int UpdateFlags[(512*1024)/UPDATEBLOCKSIZE]; /* One flag for
                                                           each block of DMAble RAM
                                                           incremented on a write */

#ifdef NEWSTYLEMEM
  /* Even though IO and ROM aren't paged finely we still use 4K entries for
     them just so it makes the look up in one pass

     1st sub is priveliged (1) for priv
     2nd sub is OS setting in MEMC (1) for OS mode
     */
  pageDesc pt[2][2][16384]; /* That's 2^26 / 4096 */
#else
  int TmpPage; /* Holds the page number from the FindPageTableEntry */
               /* in checkabort                                     */
  unsigned int OldAddress1,OldPage1; /* TLAB */
#endif

  ARMEmuFunc *ROMHighFuncs;
  ARMEmuFunc *ROMLowFuncs;
  ARMEmuFunc *PhysRamfuncs;
};


extern struct MEMCStruct memc;


typedef struct {
  struct FDCStruct FDCData;

  struct HDCStruct HDCData;

  DisplayInfo Display;

  unsigned int irqflags,fiqflags;
} PrivateDataType;


typedef enum {
  IntSource_IOC
} IntSources;


typedef enum {
  FiqSource_IOC
} FiqSources;

#define PRIVD ((PrivateDataType *)emu_state->MemDataPtr)
#define MEMC (memc)
#define ARCPGINFO(priv,os,addr) (MEMC.pt[priv][os][addr>>12])

/* ------------------- access routines for other sections of code ------------- */
/* Returns a byte in the keyboard serialiser tx register - or -1 if there isn't
   one. Clears the tx register full flag                                        */
int ArmArc_ReadKbdTx(ARMul_State *state);


/* Places a byte in the keyboard serialiser rx register; returns -1 if its
   still full.                                                                  */
int ArmArc_WriteKbdRx(ARMul_State *state, unsigned char value);

void EnableTrace(void);

int FindPageTableEntry(unsigned address, ARMword *PageTabVal);

ARMword GetPhysAddress(unsigned int address);

#ifdef SOUND_SUPPORT
int SoundDMAFetch(SoundData *buffer);
void SoundUpdateStereoImage(void);
void SoundUpdateSampleRate(void);
#endif


#endif
