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

#include <stdio.h>
#include <stdlib.h>

#define FALSE 0
#define TRUE 1
#define LOW 0
#define HIGH 1
#define LOWHIGH 1
#define HIGHLOW 2

#ifndef __STDC__
typedef char * VoidStar;
#endif

typedef unsigned int ARMword; /* must be 32 bits wide */

typedef struct ARMul_State ARMul_State;
extern ARMul_State statestr;

typedef unsigned ARMul_CPInits(ARMul_State *state);
typedef unsigned ARMul_CPExits(ARMul_State *state);
typedef unsigned ARMul_LDCs(ARMul_State *state, unsigned type, ARMword instr, ARMword value);
typedef unsigned ARMul_STCs(ARMul_State *state, unsigned type, ARMword instr, ARMword *value);
typedef unsigned ARMul_MRCs(ARMul_State *state, unsigned type, ARMword instr, ARMword *value);
typedef unsigned ARMul_MCRs(ARMul_State *state, unsigned type, ARMword instr, ARMword value);
typedef unsigned ARMul_CDPs(ARMul_State *state, unsigned type, ARMword instr);
typedef unsigned ARMul_CPReads(ARMul_State *state, unsigned reg, ARMword *value);
typedef unsigned ARMul_CPWrites(ARMul_State *state, unsigned reg, ARMword value);


typedef enum ARMStartIns {
  SEQ           = 0,
  NONSEQ        = 1,
  PCINCEDSEQ    = 2,
  PCINCEDNONSEQ = 3,
  PRIMEPIPE     = 4,
  RESUME        = 8
} ARMStartIns;


struct ARMul_State {
   /* Most common stuff, current register file first to ease indexing */
   ARMword Reg[16];           /* the current register file */
   unsigned long NumCycles;   /* Number of cycles */
   enum ARMStartIns NextInstr;/* Pipeline state */
#ifdef MEMC_IN_STATE
   struct MEMCStruct *MEMCPtr;/* MEMC */
#endif
   unsigned abortSig;         /* Abort state */
   ARMword Aborted;           /* sticky flag for aborts */
   ARMword AbortAddr;         /* to keep track of Prefetch aborts */
   unsigned Exception;        /* IRQ & FIQ pins */
   unsigned long Now;         /* time next event should be triggered */
   void *MemDataPtr;          /* VIDC/DisplayInfo struct */
   ARMword Bank;              /* the current register bank */
   unsigned NtransSig;        /* MEMC USR/SVC flag, somewhat redundant with FastMapMode */
   ARMword Base;              /* extra hand for base writeback */

   /* Less common stuff */   
   ARMword RegBank[4][16];    /* all the registers */
   ARMword instr, pc, temp;   /* saved register state */
   ARMword loaded, decoded;   /* saved pipeline state */

   /* Rare stuff */
   ARMul_CPInits *CPInit[16]; /* coprocessor initialisers */
   ARMul_CPExits *CPExit[16]; /* coprocessor finalisers */
   ARMul_LDCs *LDC[16];       /* LDC instruction */
   ARMul_STCs *STC[16];       /* STC instruction */
   ARMul_MRCs *MRC[16];       /* MRC instruction */
   ARMul_MCRs *MCR[16];       /* MCR instruction */
   ARMul_CDPs *CDP[16];       /* CDP instruction */
   ARMul_CPReads *CPRead[16]; /* Read CP register */
   ARMul_CPWrites *CPWrite[16]; /* Write CP register */
   unsigned char *CPData[16]; /* Coprocessor data */

 };

#define LATEABTSIG LOW


/***************************************************************************\
*                   Macros to extract instruction fields                    *
\***************************************************************************/

#define BIT(n) ( (ARMword)(instr>>(n))&1)   /* bit n of instruction */
#define BITS(m,n) ( (ARMword)(instr<<(31-(n))) >> ((31-(n))+(m)) ) /* bits m to n of instr */
#define TOPBITS(n) (instr >> (n)) /* bits 31 to n of instr */

/***************************************************************************\
*                      The hardware vector addresses                        *
\***************************************************************************/

#define ARMResetV 0L
#define ARMUndefinedInstrV 4L
#define ARMSWIV 8L
#define ARMPrefetchAbortV 12L
#define ARMDataAbortV 16L
#define ARMAddrExceptnV 20L
#define ARMIRQV 24L
#define ARMFIQV 28L
#define ARMErrorV 32L /* This is an offset, not an address ! */

#define ARMul_ResetV ARMResetV
#define ARMul_UndefinedInstrV ARMUndefinedInstrV
#define ARMul_SWIV ARMSWIV
#define ARMul_PrefetchAbortV ARMPrefetchAbortV
#define ARMul_DataAbortV ARMDataAbortV
#define ARMul_AddrExceptnV ARMAddrExceptnV
#define ARMul_IRQV ARMIRQV
#define ARMul_FIQV ARMFIQV

/***************************************************************************\
*                          Mode and Bank Constants                          *
\***************************************************************************/

#define USER26MODE 0L
#define FIQ26MODE 1L
#define IRQ26MODE 2L
#define SVC26MODE 3L

#define USERBANK USER26MODE
#define FIQBANK FIQ26MODE
#define IRQBANK IRQ26MODE
#define SVCBANK SVC26MODE

/***************************************************************************\
*                  Definitons of things in the emulator                     *
\***************************************************************************/

void ARMul_EmulateInit(void);
ARMul_State *ARMul_NewState(void);
void ARMul_Reset(ARMul_State *state);
ARMword ARMul_DoProg(ARMul_State *state);

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

extern unsigned ARMul_MemoryInit(ARMul_State *state);
extern void ARMul_MemoryExit(ARMul_State *state);

#if 0
extern ARMword ARMul_LoadWordS(ARMul_State *state,ARMword address);
extern ARMword ARMul_LoadWordN(ARMul_State *state,ARMword address);
extern ARMword ARMul_LoadByte(ARMul_State *state,ARMword address);

extern void ARMul_StoreWordS(ARMul_State *state,ARMword address, ARMword data);
extern void ARMul_StoreWordN(ARMul_State *state,ARMword address, ARMword data);
extern void ARMul_StoreByte(ARMul_State *state,ARMword address, ARMword data);

extern ARMword ARMul_SwapWord(ARMul_State *state,ARMword address, ARMword data);
extern ARMword ARMul_SwapByte(ARMul_State *state,ARMword address, ARMword data);

extern ARMword ARMul_MemAccess(ARMul_State *state,ARMword,ARMword,ARMword,
                  ARMword,ARMword,ARMword,ARMword,ARMword,ARMword,ARMword);
#endif

/***************************************************************************\
*            Definitons of things in the co-processor interface             *
\***************************************************************************/

#define ARMul_FIRST 0
#define ARMul_TRANSFER 1
#define ARMul_BUSY 2
#define ARMul_DATA 3
#define ARMul_INTERRUPT 4
#define ARMul_DONE 0
#define ARMul_CANT 1
#define ARMul_INC 3

extern unsigned ARMul_CoProInit(ARMul_State *state);
extern void ARMul_CoProExit(ARMul_State *state);
extern void ARMul_CoProAttach(ARMul_State *state, unsigned number,
                              ARMul_CPInits *init, ARMul_CPExits *exits,
                              ARMul_LDCs *ldc, ARMul_STCs *stc,
                              ARMul_MRCs *mrc, ARMul_MCRs *mcr,
                              ARMul_CDPs *cdp,
                              ARMul_CPReads *reads, ARMul_CPWrites *writes);
extern void ARMul_CoProDetach(ARMul_State *state, unsigned number);

/***************************************************************************\
*                            Host-dependent stuff                           *
\***************************************************************************/

#ifdef macintosh
pascal void SpinCursor(short increment);        /* copied from CursorCtl.h */
# define HOURGLASS           SpinCursor( 1 )
# define HOURGLASS_RATE      1023   /* 2^n - 1 */
#endif

#include "arch/archio.h"
#include "arch/armarc.h"

#endif
