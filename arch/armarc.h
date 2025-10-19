#ifndef ARMARC_HEADER
#define ARMARC_HEADER
/* (c) David Alan Gilbert 1995-1998 - see Readme file for copying info */

#include "../armdefs.h"
#include "archio.h"
#include "fdc1772.h"
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


/**
 * 0 means ROM is mapped as normal, 1 means
 * processor hasn't done access to low memory, 2 means it
 * hasn't done an access to ROM space, so in 1 & 2 accesses
 * to low mem access ROM
 */
typedef enum MapFlag {
  MapFlag_Normal        = 0,
  MapFlag_UnaccessedLow = 1,
  MapFlag_UnaccessedROM = 2
} MapFlag;

/* Page size flags */
typedef enum PageSize {
  MEMC_PAGESIZE_O_4K  = 0,
  MEMC_PAGESIZE_1_8K  = 1,
  MEMC_PAGESIZE_2_16K = 2,
  MEMC_PAGESIZE_3_32K = 3
} PageSize;

struct MEMCStruct {
  ARMword *ROMHigh;           /* ROM high and low are to seperate rom areas */
  ARMword ROMHighMask;
  ARMword ROMHighSize;        /* For mapping in ROMs of different types eg. */
  ARMword *ROMLow;            /* 8bit Rom and 32bit Rom, or access speeds */ 
  ARMword ROMLowMask;
  ARMword ROMLowSize;
  ARMword *PhysRam;
  ARMword RAMSize;
  ARMword RAMMask;

  uint_least16_t ControlReg;
  uint_least16_t Vinit,Vstart,Vend,Cinit;

  /* Sound buffer addresses */
  uint_least16_t Sstart; /* The start and end of the next buffer */
  uint_least16_t SendN;
  uint_least16_t Sptr; /* Current position in current buffer */
  uint_least16_t SendC; /* End of current buffer */
  uint_least16_t SstartC; /* The start of the current buffer */
  bool NextSoundBufferValid; /* This indicates whether the next buffer has been set yet */

  MapFlag ROMMapFlag;

  PageSize DRAMPageSize; /* Page size we pretend our DRAM is */
  PageSize PageSizeFlags;

  /* Fastmap memory block pointers */
  void *ROMRAMChunk;
#ifdef ARMUL_INSTR_FUNC_CACHE
  void *EmuFuncChunk;
#endif

  int32_t PageTable[512]; /* Good old fashioned MEMC1 page table */

  uint32_t UpdateFlags[(512*1024)/UPDATEBLOCKSIZE]; /* One flag for
                                                       each block of DMAble RAM
                                                       incremented on a write */
};


extern struct MEMCStruct memc;

#define MEMC (memc)

void ARMul_RebuildFastMap(ARMul_State *state);

#endif
