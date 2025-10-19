/*  armdefs.h -- ARMulator common definitions:  ARM6 Instruction Emulator.
    Copyright (C) 1994 Advanced RISC Machines Ltd.

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

/* DAG - added header recursion tests */
#ifndef ARMDEFS_HEADER
#define ARMDEFS_HEADER

#include "c99.h"

/* Control caching of instruction handler functions */
#define ARMUL_INSTR_FUNC_CACHE

/* Support coprocessors for ARM3 cache control */
#define ARMUL_COPRO_SUPPORT

typedef uint32_t ARMword; /* must be 32 bits wide */

typedef struct ARMul_State ARMul_State;
extern ARMul_State statestr;

typedef void (*ARMEmuFunc)(ARMul_State *state, ARMword instr);

#define LOW false
#define HIGH true

#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))

#define UNUSED_VAR(x) (void)(x)

#define UPDATEBLOCKSIZE 256

/***************************************************************************\
*                   Macros to extract instruction fields                    *
\***************************************************************************/

#define BIT(n) ( (ARMword)(instr>>(n))&1)   /* bit n of instruction */
#define BITS(m,n) ( (ARMword)(instr<<(31-(n))) >> ((31-(n))+(m)) ) /* bits m to n of instr */
#define TOPBITS(n) (instr >> (n)) /* bits 31 to n of instr */

/***************************************************************************\
*                      The hardware vector addresses                        *
\***************************************************************************/

typedef enum ARMAbort {
  ARMul_ResetV = 0,
  ARMul_UndefinedInstrV = 4,
  ARMul_SWIV = 8,
  ARMul_PrefetchAbortV = 12,
  ARMul_DataAbortV = 16,
  ARMul_AddrExceptnV = 20,
  ARMul_IRQV = 24,
  ARMul_FIQV = 28,
  ARMul_ErrorV = 32 /* This is an offset, not an address ! */
} ARMAbort;

/***************************************************************************\
*                          Mode and Bank Constants                          *
\***************************************************************************/

typedef enum ARMBank {
  USER26MODE = 0,
  FIQ26MODE = 1,
  IRQ26MODE = 2,
  SVC26MODE = 3,

  USERBANK = USER26MODE,
  FIQBANK = FIQ26MODE,
  IRQBANK = IRQ26MODE,
  SVCBANK = SVC26MODE
} ARMBank;

/***************************************************************************\
*                          Fastmap definitions                              *
\***************************************************************************/

/* The fast map encodes the page access flags, memory pointer, and read/write
   function pointers into just two words of data (8 bytes on 32bit platforms,
   16 bytes on 64bit)
   The first word contains the access flags and memory pointer
   The second word contains the read/write func pointer (combined function
   under the assumption that MMIO read/write will be infrequent) 
*/

#ifndef FASTMAP_64
#if defined(__LP64__) || defined(_WIN64)
#define FASTMAP_64
#endif
#endif

typedef intptr_t FastMapInt;
typedef uintptr_t FastMapUInt;

#ifdef FASTMAP_64
#define FASTMAP_FLAG(X) (((FastMapUInt)(X))<<56) /* Shift a byte to the top byte of the word */
#else
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
#define FASTMAP_ACCESSFUNC_BYTE        0x02UL /* Only relevant for writes */
#define FASTMAP_ACCESSFUNC_STATECHANGE 0x04UL /* Only relevant for writes */

#ifdef ARMUL_INSTR_FUNC_CACHE
#define FASTMAP_CLOBBEREDFUNC 0 /* Value written when a func gets clobbered */
#endif

typedef FastMapInt FastMapRes; /* Result of a DecodeRead/DecodeWrite function */

typedef ARMword (*FastMapAccessFunc)(ARMul_State *state,ARMword addr,ARMword data,ARMword flags);

typedef struct {
  FastMapUInt FlagsAndData;
  FastMapAccessFunc AccessFunc;
} FastMapEntry;

/***************************************************************************\
*                               Event queue                                 *
\***************************************************************************/

/* The event queue is a simple priority queue used for all time-based events
   external to the CPU. Updating IOC timers, frontend screen output, etc.

   The CPU cycle counter is the timer used to trigger the events.
   
   When an event is triggered, it must either remove itself (via EventQ_Remove)
   or reschedule itself for a different time (EventQ_RescheduleHead). Failure
   to do either of those will result in an infinite loop where the same event
   is repeatedly triggered without the CPU emulation advancing.

   It's also acceptable for an event to remove itself and schedule several
   other events, in its place. Just be careful about the order events are
   inserted, otherwise when you call Remove(0) or RescheduleHead() the head
   may not be the same as before. Also be wary of timer overflows; any event
   scheduled for more than MAX_CYCLES_INTO_FUTURE cycles into the future will
   be considered as having already been triggered and will fire immediately.
 */

typedef uint32_t CycleCount;
typedef int32_t CycleDiff;
#define MAX_CYCLES_INTO_FUTURE INT32_MAX

typedef void (*EventQ_Func)(ARMul_State *state,CycleCount nowtime);

typedef struct {
  CycleCount Time;  /* When to trigger the event */
  EventQ_Func Func;    /* Function to call */
} EventQ_Entry;

#define EVENTQ_SIZE 8

/* NOTE - For speed reasons there aren't any overflow checks in the eventq
          code, so be aware of how many systems are using the queue. At the
          moment, these are:

          arch/newsound.c - One entry for sound DMA fetches
          arch/XXXdisplaydev.c - One entry for screen updates
          arch/keyboard.c - One entry for keyboard/mouse polling
          arch/archio.c - One entry for IOC timers
          arch/archio.c - One entry for FDC & HDC updates
        = 5 total
*/

/***************************************************************************\
*                          Main emulator state                              *
\***************************************************************************/

typedef enum ARMStartIns {
  NORMAL        = 0,
  PCINCED       = 1,
  PRIMEPIPE     = 2,
  RESUME        = 4
} ARMStartIns;

typedef struct arch_keyboard arch_keyboard;
typedef struct Vidc_Regs Vidc_Regs;
typedef struct ArcemConfig_s ArcemConfig;
typedef struct ARMul_CoPro ARMul_CoPro;

#define Exception_IRQ (UINT32_C(1) << 27)
#define Exception_FIQ (UINT32_C(1) << 26)

struct ARMul_State {
   /* Most common stuff, current register file first to ease indexing */
   ARMword Reg[16];           /* the current register file */
   CycleCount NumCycles;      /* Number of cycles */
   ARMStartIns NextInstr;     /* Pipeline state */
   ARMBank Bank;              /* the current register bank */
   uint_least8_t ExitCode;    /* return code used when terminating the emulator */
   bool KillEmulator;         /* global used to terminate the emulator */
   bool OSmode;               /* MEMC USR/OS flag, somewhat redundant with FastMapMode */
   bool NtransSig;            /* MEMC USR/SVC flag, somewhat redundant with FastMapMode */
   bool abortSig;             /* Abort state */
   ARMAbort Aborted;          /* sticky flag for aborts */
   ARMword AbortAddr;         /* to keep track of Prefetch aborts */
   ARMword Exception;         /* IRQ & FIQ pins */
   ARMword Base;              /* extra hand for base writeback */
   Vidc_Regs *Display;        /* VIDC regs/host display struct */
   arch_keyboard *Kbd;        /* Keyboard struct */
   ArcemConfig *Config;

   /* Fastmap stuff */
   FastMapUInt FastMapMode;   /* Current access mode flags */
#ifdef ARMUL_INSTR_FUNC_CACHE
   FastMapUInt FastMapInstrFuncOfs; /* Offset between the RAM/ROM data and the ARMEmuFunc data */
#endif
   FastMapEntry *FastMap;

   /* Event queue */
   EventQ_Entry EventQ[EVENTQ_SIZE];
   uint_least8_t NumEvents;

   /* Enabled CPU features */
   bool HasSWP, HasCP15;

   /* Less common stuff */   
   ARMword instr, pc;         /* saved register state */
   ARMword loaded, decoded;   /* saved pipeline state */
   ARMword RegBank[4][16];    /* all the registers */

#ifdef ARMUL_COPRO_SUPPORT
   /* Rare stuff */
   const ARMul_CoPro *CoPro[16]; /* coprocessor interface */
#endif
 };

#ifdef AMIGA
extern void *state_alloc(int s);
extern void state_free(void *p);
#else
/* If you need special allocation for the state rather than
 * using the usual static global, you can override these functions
 * and provide your own.
 */
static inline void *state_alloc(int s)
{
	return &statestr;
}

static inline void state_free(void *p)
{
}
#endif
 
/***************************************************************************\
*                  Definitons of things in the emulator                     *
\***************************************************************************/

void ARMul_EmulateInit(void);
ARMul_State *ARMul_NewState(ArcemConfig *pConfig);
void ARMul_FreeState(ARMul_State *state);
void ARMul_Reset(ARMul_State *state);
void ARMul_Exit(ARMul_State *state, uint_least8_t exit_code);
int ARMul_DoProg(ARMul_State *state);

/***************************************************************************\
*                Definitons of things for event handling                    *
\***************************************************************************/

#define ARMul_Time (state->NumCycles)

/***************************************************************************\
*                          Useful support routines                          *
\***************************************************************************/

void ARMul_SetReg(ARMul_State *state, unsigned mode, unsigned reg, ARMword value);
ARMword ARMul_GetPC(ARMul_State *state);
ARMword ARMul_GetNextPC(ARMul_State *state);
void ARMul_SetPC(ARMul_State *state, ARMword value);
ARMword ARMul_GetR15(ARMul_State *state);
void ARMul_SetR15(ARMul_State *state, ARMword value);

/***************************************************************************\
*                  Definitons of things to handle aborts                    *
\***************************************************************************/

extern void ARMul_Abort(ARMul_State *state, ARMword address);
#define ARMul_ABORTWORD 0xefffffff /* SWI -1 */
#define ARMul_PREFETCHABORT(address) if (state->AbortAddr == 1) \
                                        state->AbortAddr = ((address) & ~3L)
#define ARMul_DATAABORT(address) state->abortSig = HIGH; \
                                 state->Aborted = ARMul_DataAbortV;
#define ARMul_CLEARABORT state->abortSig = LOW

/***************************************************************************\
*              Definitons of things in the memory interface                 *
\***************************************************************************/

extern bool ARMul_MemoryInit(ARMul_State *state);
extern void ARMul_MemoryExit(ARMul_State *state);

/***************************************************************************\
*                               ARM Support                                 *
\***************************************************************************/

/* An estimate of how many cycles the host is executing per second */
extern uint32_t ARMul_EmuRate;

/* Reset the EmuRate code, to cope with situations where the emulator has just been resumed after being suspended for a period of time (i.e. > 1 second) */
void EmuRate_Reset(ARMul_State *state);

/* Update the EmuRate value. Note: Manipulates event queue! */
void EmuRate_Update(ARMul_State *state);

#endif
