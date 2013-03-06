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


typedef void (*ARMEmuFunc)(ARMul_State *state, ARMword instr);

ARMEmuFunc ARMul_Emulate_DecodeInstr(ARMword instr);

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

  int32_t PageTable[512]; /* Good old fashioned MEMC1 page table */
  ARMword ControlReg;
  ARMword PageSizeFlags;
  ARMword Vinit,Vstart,Vend,Cinit;

  /* Sound buffer addresses */
  ARMword Sstart; /* The start and end of the next buffer */
  ARMword SendN;
  ARMword Sptr; /* Current position in current buffer */
  ARMword SendC; /* End of current buffer */
  ARMword SstartC; /* The start of the current buffer */
  bool NextSoundBufferValid; /* This indicates whether the next buffer has been set yet */

  uint_fast8_t ROMMapFlag; /* 0 means ROM is mapped as normal,1 means
                     processor hasn't done access to low memory, 2 means it
                     hasn't done an access to ROM space, so in 1 & 2 accesses
                     to  low mem access ROM */

  ARMword DRAMPageSize; /* Page size we pretend our DRAM is */ 

  uint32_t UpdateFlags[(512*1024)/UPDATEBLOCKSIZE]; /* One flag for
                                                       each block of DMAble RAM
                                                       incremented on a write */

  /* Fastmap memory block pointers */
  void *ROMRAMChunk;
  void *EmuFuncChunk;
};


extern struct MEMCStruct memc;

#define MEMC (memc)

/* ------------------- inlined FastMap functions ------------------------------ */
static FastMapEntry *FastMap_GetEntry(ARMul_State *state,ARMword addr);
static FastMapEntry *FastMap_GetEntryNoWrap(ARMul_State *state,ARMword addr);
static FastMapRes FastMap_DecodeRead(const FastMapEntry *entry,FastMapUInt mode);
static FastMapRes FastMap_DecodeWrite(const FastMapEntry *entry,FastMapUInt mode);
static ARMword *FastMap_Log2Phy(const FastMapEntry *entry,ARMword addr);
static ARMEmuFunc *FastMap_Phy2Func(ARMul_State *state,ARMword *addr);
static ARMword FastMap_LoadFunc(const FastMapEntry *entry,ARMul_State *state,ARMword addr);
static void FastMap_StoreFunc(const FastMapEntry *entry,ARMul_State *state,ARMword addr,ARMword data,ARMword flags);
static void FastMap_RebuildMapMode(ARMul_State *state);

static inline FastMapEntry *FastMap_GetEntry(ARMul_State *state,ARMword addr)
{
	/* Return FastMapEntry corresponding to this address */
	return &state->FastMap[(addr&~UINT32_C(0xFC000000))>>12];
}

static inline FastMapEntry *FastMap_GetEntryNoWrap(ARMul_State *state,ARMword addr)
{
	/* Return FastMapEntry corresponding to this address, without performing address wrapping (use with caution!) */
	return &state->FastMap[addr>>12];
}

static inline FastMapRes FastMap_DecodeRead(const FastMapEntry *entry,FastMapUInt mode)
{
	/* Use the FASTMAP_RESULT_* macros to decide what to do */
	return ((FastMapRes)((entry->FlagsAndData)&mode));
}

static inline FastMapRes FastMap_DecodeWrite(const FastMapEntry *entry,FastMapUInt mode)
{
	/* Use the FASTMAP_RESULT_* macros to decide what to do */
	return ((FastMapRes)((entry->FlagsAndData<<1)&mode));
}

static inline ARMword *FastMap_Log2Phy(const FastMapEntry *entry,ARMword addr)
{
	/* Returns physical (i.e. real) pointer assuming read/write check returned >0 and addr is properly within range */
	return (ARMword*)(((FastMapUInt)addr)+(entry->FlagsAndData<<8));
}

static inline ARMEmuFunc *FastMap_Phy2Func(ARMul_State *state,ARMword *addr)
{
	/* Return ARMEmuFunc * for an address returned by Log2Phy */
#ifdef FASTMAP_64
	/* Shift addr so we access ARMEmuFunc *'s as 64bit data types instead of 32bit */
	return (ARMEmuFunc*)((((FastMapUInt)addr)<<1)+state->FastMapInstrFuncOfs);
#else
	return (ARMEmuFunc*)(((FastMapUInt)addr)+state->FastMapInstrFuncOfs);
#endif
}

static inline ARMword FastMap_LoadFunc(const FastMapEntry *entry,ARMul_State *state,ARMword addr)
{
	/* Return load result, assumes it's a func */
	return (entry->AccessFunc)(state,addr,0,0);
}

static inline void FastMap_StoreFunc(const FastMapEntry *entry,ARMul_State *state,ARMword addr,ARMword data,ARMword flags)
{
	/* Perform store, assumes it's a func */
	(entry->AccessFunc)(state,addr,data,flags | FASTMAP_ACCESSFUNC_WRITE);
}

static inline void FastMap_RebuildMapMode(ARMul_State *state)
{
	state->FastMapMode = (state->NtransSig?FASTMAP_MODE_MBO|FASTMAP_MODE_SVC:(MEMC.ControlReg&(1<<12))?FASTMAP_MODE_MBO|FASTMAP_MODE_OS:FASTMAP_MODE_MBO|FASTMAP_MODE_USR);
}

/* Macros to evaluate DecodeRead/DecodeWrite results
   These are all mutually exclusive, so a standard DIRECT -> FUNC -> ABORT check doesn't need to explicitly check for abort */
#define FASTMAP_RESULT_DIRECT(res) ((res) > 0)
#define FASTMAP_RESULT_FUNC(res) (((FastMapUInt)(res)) > FASTMAP_MODE_MBO)
#define FASTMAP_RESULT_ABORT(res) (((res)<<1)==0)

/* ------------------- inlined higher-level memory funcs ---------------------- */

#define FASTMAP_INLINE

#ifdef FASTMAP_INLINE
#define FASTMAP_PROTO static
#else
#define FASTMAP_PROTO extern
#endif

FASTMAP_PROTO ARMword ARMul_LoadWordS(ARMul_State *state,ARMword address);
FASTMAP_PROTO ARMword ARMul_LoadByte(ARMul_State *state,ARMword address);
FASTMAP_PROTO void ARMul_StoreWordS(ARMul_State *state, ARMword address, ARMword data);
FASTMAP_PROTO void ARMul_StoreByte(ARMul_State *state, ARMword address, ARMword data);
FASTMAP_PROTO ARMword ARMul_SwapWord(ARMul_State *state, ARMword address, ARMword data);
FASTMAP_PROTO ARMword ARMul_SwapByte(ARMul_State *state, ARMword address, ARMword data);

#ifdef FASTMAP_INLINE
#include "arch/fastmap.c"
#endif

/**
 * ARMul_LoadWordN
 *
 * Load Word, Non Sequential Cycle
 *
 * @param state
 * @param address
 * @returns
 */
#define ARMul_LoadWordN ARMul_LoadWordS /* These were 100% equivalent in the original implementation! */

/**
 * ARMul_StoreWordN
 *
 * Store Word, Non Sequential Cycle
 *
 * @param state
 * @param address
 * @param data
 */
#define ARMul_StoreWordN ARMul_StoreWordS /* These were 100% equivalent in the original implementation! */

/* ------------------- access routines for other sections of code ------------- */
/* Returns a byte in the keyboard serialiser tx register - or -1 if there isn't
   one. Clears the tx register full flag                                        */
int ArmArc_ReadKbdTx(ARMul_State *state);


/* Places a byte in the keyboard serialiser rx register; returns -1 if its
   still full.                                                                  */
int ArmArc_WriteKbdRx(ARMul_State *state, unsigned char value);


void ARMul_RebuildFastMap(ARMul_State *state);

#endif
