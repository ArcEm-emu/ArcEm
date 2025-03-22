/*  armcopro.h -- co-processor interface:  ARM6 Instruction Emulator.
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
#ifndef ARMCOPRO_HEADER
#define ARMCOPRO_HEADER

#include "armdefs.h"

#ifdef ARMUL_COPRO_SUPPORT

/***************************************************************************\
*            Definitons of things in the co-processor interface             *
\***************************************************************************/

typedef bool ARMul_CPInits(ARMul_State *state);
typedef bool ARMul_CPExits(ARMul_State *state);
typedef unsigned ARMul_LDCs(ARMul_State *state, unsigned type, ARMword instr, ARMword value);
typedef unsigned ARMul_STCs(ARMul_State *state, unsigned type, ARMword instr, ARMword *value);
typedef unsigned ARMul_MRCs(ARMul_State *state, unsigned type, ARMword instr, ARMword *value);
typedef unsigned ARMul_MCRs(ARMul_State *state, unsigned type, ARMword instr, ARMword value);
typedef unsigned ARMul_CDPs(ARMul_State *state, unsigned type, ARMword instr);
typedef bool ARMul_CPReads(ARMul_State *state, unsigned reg, ARMword *value);
typedef bool ARMul_CPWrites(ARMul_State *state, unsigned reg, ARMword value);

struct ARMul_CoPro {
   ARMul_CPInits *CPInit;   /* coprocessor initialisers */
   ARMul_CPExits *CPExit;   /* coprocessor finalisers */
   ARMul_LDCs *LDC;         /* LDC instruction */
   ARMul_STCs *STC;         /* STC instruction */
   ARMul_MRCs *MRC;         /* MRC instruction */
   ARMul_MCRs *MCR;         /* MCR instruction */
   ARMul_CDPs *CDP;         /* CDP instruction */
   ARMul_CPReads *CPRead;   /* Read CP register */
   ARMul_CPWrites *CPWrite; /* Write CP register */
 };

#define ARMul_FIRST 0
#define ARMul_TRANSFER 1
#define ARMul_BUSY 2
#define ARMul_DATA 3
#define ARMul_INTERRUPT 4
#define ARMul_DONE 0
#define ARMul_CANT 1
#define ARMul_INC 3

extern bool ARMul_CoProInit(ARMul_State *state);
extern void ARMul_CoProExit(ARMul_State *state);
extern void ARMul_CoProAttach(ARMul_State *state, unsigned number,
                              const ARMul_CoPro *copro);
extern void ARMul_CoProDetach(ARMul_State *state, unsigned number);

extern void ARMul_LDC(ARMul_State *state,ARMword instr,ARMword address);
extern void ARMul_STC(ARMul_State *state,ARMword instr,ARMword address);
extern void ARMul_MCR(ARMul_State *state,ARMword instr, ARMword source);
extern bool ARMul_MRC(ARMul_State *state,ARMword instr,ARMword *result);
extern void ARMul_CDP(ARMul_State *state,ARMword instr);

/***************************************************************************\
*                            Dummy Co-processors                            *
\***************************************************************************/

unsigned ARMul_NoCoPro3R(ARMul_State *state,unsigned,ARMword);
unsigned ARMul_NoCoPro4R(ARMul_State *state,unsigned,ARMword,ARMword);
unsigned ARMul_NoCoPro4W(ARMul_State *state,unsigned,ARMword,ARMword *);

#else
#include "armemu.h"

static inline void ARMul_LDC(ARMul_State *state,ARMword instr,ARMword address)
{
 UNDEF_LSCPCBaseWb;
 if (ADDREXCEPT(address)) {
    INTERNALABORT(address);
    }
 CPTAKEABORT;
}

static inline void ARMul_STC(ARMul_State *state,ARMword instr,ARMword address)
{
 UNDEF_LSCPCBaseWb;
 if (ADDREXCEPT(address)) {
    INTERNALABORT(address);
    }
 CPTAKEABORT;
}

static inline void ARMul_MCR(ARMul_State *state,ARMword instr, ARMword source)
{
 ARMul_Abort(state,ARMul_UndefinedInstrV);
}

static inline bool ARMul_MRC(ARMul_State *state,ARMword instr,ARMword *result)
{
 ARMul_Abort(state,ARMul_UndefinedInstrV);
 return false;
}

static inline void ARMul_CDP(ARMul_State *state,ARMword instr)
{
 ARMul_Abort(state,ARMul_UndefinedInstrV);
}

#endif

#endif
