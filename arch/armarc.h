#ifndef ARMARC_HEADER
#define ARMARC_HEADER
/* (c) David Alan Gilbert 1995-1998 - see Readme file for copying info */

#include "../armdefs.h"
#include "../armopts.h"
#include "DispKbd.h"
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


/* Erm - this has to be 256....*/
#define UPDATEBLOCKSIZE 256

typedef void (*ARMWriteFunc)(ARMul_State* state, ARMword addr, ARMword data, int bNw);
typedef ARMword (*ARMReadFunc)(ARMul_State* state, ARMword addr);
typedef void (*ARMEmuFunc)(ARMul_State *state, ARMword instr, ARMword pc);

ARMEmuFunc ARMul_Emulate_DecodeInstr(ARMword instr);

/* The fast map encodes the page access flags, memory pointer, and read/write function pointers into just two words of data (8 bytes on 32bit platforms, 16 bytes on 64bit)
   The first word contains the access flags and memory pointer
   The second word contains the read/write func pointer (combined function under the assumption that MMIO read/write will be infrequent)
*/

#ifndef FASTMAP_64
#ifdef __LP64__
#define FASTMAP_64
#endif
#endif

#ifdef FASTMAP_64
typedef signed long int FastMapInt;
typedef unsigned long int FastMapUInt;
#define FASTMAP_FLAG(X) (((FastMapUInt)(X))<<56) /* Shift a byte to the top byte of the word */
#else
typedef signed int FastMapInt;
typedef unsigned int FastMapUInt;
#define FASTMAP_FLAG(X) (((FastMapUInt)(X))<<24) /* Shift a byte to the top byte of the word */
#endif

#define FASTMAP_SIZE (0x4000000/4096)

#define FASTMAP_R_FUNC FASTMAP_FLAG(0x80) /* Use function for reading */
#define FASTMAP_W_FUNC FASTMAP_FLAG(0x40) /* Use function for writing */
#define FASTMAP_R_SVC  FASTMAP_FLAG(0x20) /* Page has SVC read access */
#define FASTMAP_W_SVC  FASTMAP_FLAG(0x10) /* Page has SVC write access */
#define FASTMAP_R_OS   FASTMAP_FLAG(0x08) /* Page has OS read access */
#define FASTMAP_W_OS   FASTMAP_FLAG(0x04) /* Page has OS write access */
#define FASTMAP_R_USR  FASTMAP_FLAG(0x02) /* Page has USR read access */
#define FASTMAP_W_USR  FASTMAP_FLAG(0x01) /* Page has USR write access */

#define FASTMAP_MODE_USR FASTMAP_FLAG(0x02) /* We are in user mode */
#define FASTMAP_MODE_OS  FASTMAP_FLAG(0x08) /* The OS flag is set in the control register */
#define FASTMAP_MODE_SVC FASTMAP_FLAG(0x20) /* We are in SVC mode */
#define FASTMAP_MODE_MBO FASTMAP_FLAG(0x80) /* Must be one! */

#define FASTMAP_ACCESSFUNC_WRITE       0x01UL
#define FASTMAP_ACCESSFUNC_BYTE        0x02UL
#define FASTMAP_ACCESSFUNC_STATECHANGE 0x04UL /* Only relevant for writes */

#define FASTMAP_CLOBBEREDFUNC 0 /* Value written when a func gets clobbered */

typedef FastMapInt FastMapRes; /* Result of a DecodeRead/DecodeWrite function */

typedef ARMword (*FastMapAccessFunc)(ARMul_State *state,ARMword addr,ARMword data,ARMword flags);

typedef struct {
  FastMapUInt FlagsAndData;
  FastMapAccessFunc AccessFunc;
} FastMapEntry;

struct MEMCStruct {
  ARMword *ROMHigh;           /* ROM high and low are to seperate rom areas */
  ARMword ROMHighMask;
  ARMword ROMHighSize;        /* For mapping in ROMs of different types eg. */
  ARMword *ROMLow;            /* 8bit Rom and 32bit Rom, or access speeds */ 
  ARMword ROMLowMask;
  ARMword ROMLowSize;
  ARMword *PhysRam;
  unsigned int RAMSize;
  ARMword RAMMask;

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

  FastMapUInt FastMapMode; /* Current access mode flags */
  FastMapUInt FastMapInstrFuncOfs; /* Offset between the RAM/ROM data and the ARMEmuFunc data */
  FastMapEntry FastMap[FASTMAP_SIZE];
  void *ROMRAMChunk;
  void *EmuFuncChunk;
};


extern struct MEMCStruct memc;


typedef struct {
  DisplayInfo Display;
} PrivateDataType;

#define PRIVD ((PrivateDataType *)state->MemDataPtr)
#define MEMC (memc)

/* ------------------- inlined FastMap functions ------------------------------ */
static FastMapEntry *FastMap_GetEntry(ARMword addr);
static FastMapEntry *FastMap_GetEntryNoWrap(ARMword addr);
static FastMapRes FastMap_DecodeRead(const FastMapEntry *entry,ARMword mode);
static FastMapRes FastMap_DecodeWrite(const FastMapEntry *entry,ARMword mode);
static ARMword *FastMap_Log2Phy(const FastMapEntry *entry,ARMword addr);
static ARMEmuFunc *FastMap_Phy2Func(ARMword *addr);
static ARMword FastMap_LoadFunc(const FastMapEntry *entry,ARMul_State *state,ARMword addr);
static void FastMap_StoreFunc(const FastMapEntry *entry,ARMul_State *state,ARMword addr,ARMword data,ARMword flags);
static void FastMap_RebuildMapMode(ARMul_State *state);

static inline FastMapEntry *FastMap_GetEntry(ARMword addr)
{
	/* Return FastMapEntry corresponding to this address */
	return &MEMC.FastMap[(addr&~0xFC000000UL)>>12];
}

static inline FastMapEntry *FastMap_GetEntryNoWrap(ARMword addr)
{
	/* Return FastMapEntry corresponding to this address, without performing address wrapping (use with caution!) */
	return &MEMC.FastMap[addr>>12];
}

static inline FastMapRes FastMap_DecodeRead(const FastMapEntry *entry,ARMword mode)
{
	/* Use the FASTMAP_RESULT_* macros to decide what to do */
	return ((FastMapRes)((entry->FlagsAndData)&mode));
}

static inline FastMapRes FastMap_DecodeWrite(const FastMapEntry *entry,ARMword mode)
{
	/* Use the FASTMAP_RESULT_* macros to decide what to do */
	return ((FastMapRes)((entry->FlagsAndData<<1)&mode));
}

static inline ARMword *FastMap_Log2Phy(const FastMapEntry *entry,ARMword addr)
{
	/* Returns physical (i.e. real) pointer assuming read/write check returned >0 and addr is properly within range */
	return (ARMword*)(((FastMapUInt)addr)+(entry->FlagsAndData<<8));
}

static inline ARMEmuFunc *FastMap_Phy2Func(ARMword *addr)
{
	/* Return ARMEmuFunc * for an address returned by Log2Phy */
	return (ARMEmuFunc*)(((FastMapUInt)addr)+MEMC.FastMapInstrFuncOfs);
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
	MEMC.FastMapMode = (state->NtransSig?FASTMAP_MODE_MBO|FASTMAP_MODE_SVC:(MEMC.ControlReg&(1<<12))?FASTMAP_MODE_MBO|FASTMAP_MODE_OS:FASTMAP_MODE_MBO|FASTMAP_MODE_USR);
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

#ifdef SOUND_SUPPORT
int SoundDMAFetch(SoundData *buffer, ARMul_State *state);
void SoundUpdateStereoImage(ARMul_State *state);
void SoundUpdateSampleRate(ARMul_State *state);
#endif


#endif
