/*  arminit.c -- ARMulator initialization:  ARM6 Instruction Emulator.
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

#include <stdio.h>
#include "armdefs.h"
#include "armemu.h"
#include "armarc.h"

extern ARMword *armflags;

/***************************************************************************\
*                 Definitions for the emulator architecture                 *
\***************************************************************************/

unsigned int ARMul_MultTable[32] = { 1,  2,  2,  3,  3,  4,  4,  5,
                                     5,  6,  6,  7,  7,  8,  8,  9,
                                     9, 10, 10, 11, 11, 12, 12, 13,
                                    13, 14, 14, 15, 15, 16, 16, 16};

ARMword ARMul_ImmedTable[4096]; /* immediate DP LHS values */
char ARMul_BitList[256]; /* number of bits in a byte table */

/***************************************************************************\
*         Call this routine once to set up the emulator's tables.           *
\***************************************************************************/

void ARMul_EmulateInit(void) {
  unsigned int i, j;

  for (i = 0; i < 4096; i++) { /* the values of 12 bit dp rhs's */
    ARMul_ImmedTable[i] = ROTATER(i & 0xffL,(i >> 7L) & 0x1eL);
  }

  for (i = 0; i < 256; ARMul_BitList[i++] = 0 ); /* how many bits in LSM */
  for (j = 1; j < 256; j <<= 1)
    for (i = 0; i < 256; i++)
      if ((i & j) > 0 )
         ARMul_BitList[i]++;

  for (i = 0; i < 256; i++)
    ARMul_BitList[i] *= 4; /* you always need 4 times these values */
}


/***************************************************************************\
*            Returns a new instantiation of the ARMulator's state           *
\***************************************************************************/

ARMul_State *ARMul_NewState(void)
{ARMul_State *state;
 unsigned i, j;

 state = &statestr;

 for (i = 0; i < 16; i++) {
    state->Reg[i] = 0;
    for (j = 0; j < 7; j++)
       state->RegBank[j][i] = 0;
    }
 for (i = 0; i < 7; i++)
    state->Spsr[i] = 0;
 state->Mode = 0;

 state->VectorCatch = 0;
 state->Aborted = FALSE;
 state->Reseted = FALSE;
 state->Inted = 3;
 state->LastInted = 3;

 state->MemDataPtr = NULL;
 state->MemSize = 0;

 state->OSptr = NULL;
 state->CommandLine = NULL;

 state->EventSet = 0;
 state->Now = 0;
 state->EventPtr = (struct EventNode **)malloc((unsigned)EVENTLISTSIZE *
                                               sizeof(struct EventNode *));
 for (i = 0; i < EVENTLISTSIZE; i++)
    *(state->EventPtr + i) = NULL;

 state->lateabtSig = LOW;

 ARMul_Reset(state);
 return(state);
 }

/***************************************************************************\
*       Call this routine to set ARMulator to model a certain processor     *
\***************************************************************************/
 
void ARMul_SelectProcessor(ARMul_State *state, unsigned int processor) {
  state->lateabtSig = LOW;
}

/***************************************************************************\
* Call this routine to set up the initial machine state (or perform a RESET *
\***************************************************************************/

void ARMul_Reset(ARMul_State *state)
{state->NextInstr = 0;
    state->Reg[15] = R15INTBITS | SVC26MODE;
    state->Cpsr = INTBITS | SVC26MODE;
 ARMul_CPSRAltered(state);
 state->Bank = SVCBANK;
 FLUSHPIPE;

 state->ErrorCode = 0;

 state->Exception = 0;
 state->NfiqSig = HIGH;
 state->NirqSig = HIGH;
 state->NtransSig = (state->Mode & 3) ? HIGH : LOW;
 state->abortSig = LOW;
 state->AbortAddr = 1;

 state->NumCycles = 0;
#ifdef ASIM    
  (void)ARMul_MemoryInit();
  ARMul_OSInit(state);
#endif  
}


ARMword ARMul_DoProg(ARMul_State *state) {
  ARMword pc = 0;

  ARMul_Emulate26();
  return(pc);
}

/***************************************************************************\
* This routine causes an Abort to occur, including selecting the correct    *
* mode, register bank, and the saving of registers.  Call with the          *
* appropriate vector's memory address (0,4,8 ....)                          *
\***************************************************************************/

void ARMul_Abort(ARMul_State *state, ARMword vector) {
  ARMword temp;

  state->Aborted = FALSE;

#ifdef DEBUG
 printf("ARMul_Abort: vector=0x%x\n",vector);
#endif

#ifndef NOOS
 if (ARMul_OSException(state,vector,ARMul_GetPC(state)))
    return;
#endif

  temp = R15PC | ECC | ER15INT | EMODE;

  switch (vector) {
    case ARMul_ResetV : /* RESET */
       state->Spsr[SVCBANK] = CPSR;
       SETABORT(INTBITS,SVC26MODE);
       ARMul_CPSRAltered(state);
       state->Reg[14] = temp;
       break;
 
    case ARMul_UndefinedInstrV : /* Undefined Instruction */
       state->Spsr[SVCBANK] = CPSR;
       SETABORT(IBIT,SVC26MODE);
       ARMul_CPSRAltered(state);
       state->Reg[14] = temp - 4;
       /*fprintf(stderr,"DAG: In ARMul_Abort: Taking undefined instruction trap R[14] being set to: 0x%08x\n",
               (unsigned int)(state->Reg[14])); */
       break;
 
#define ARCEM_SWI_CHUNK 0x56ac0
#define ARCEM_SWI_MISC (ARCEM_SWI_CHUNK + 0)
    case ARMul_SWIV: /* Software Interrupt */
        if ((GetWord(ARMul_GetPC(state) - 8) & 0xffffff) == ARCEM_SWI_MISC) {
            exit(statestr.Reg[0] & 0xff);
        }
       state->Spsr[SVCBANK] = CPSR;
       SETABORT(IBIT,SVC26MODE);
       ARMul_CPSRAltered(state);
       state->Reg[14] = temp - 4;
       break;
 
    case ARMul_PrefetchAbortV : /* Prefetch Abort */
       state->AbortAddr = 1;
       state->Spsr[SVCBANK] = CPSR;
       SETABORT(IBIT,SVC26MODE);
       ARMul_CPSRAltered(state);
       state->Reg[14] = temp - 4;
       break;
 
    case ARMul_DataAbortV : /* Data Abort */
       state->Spsr[SVCBANK] = CPSR;
       SETABORT(IBIT,SVC26MODE);
       ARMul_CPSRAltered(state);
       state->Reg[14] = temp - 4; /* the PC must have been incremented */
       break;
 
    case ARMul_AddrExceptnV : /* Address Exception */
       state->Spsr[SVCBANK] = CPSR;
       SETABORT(IBIT,SVC26MODE);
       ARMul_CPSRAltered(state);
       state->Reg[14] = temp - 4;
       break;
 
    case ARMul_IRQV : /* IRQ */
       state->Spsr[IRQBANK] = CPSR;
       SETABORT(IBIT,IRQ26MODE);
       ARMul_CPSRAltered(state);
       state->Reg[14] = temp - 4;
       break;
 
    case ARMul_FIQV : /* FIQ */
       state->Spsr[FIQBANK] = CPSR;
       SETABORT(INTBITS,FIQ26MODE);
       ARMul_CPSRAltered(state);
       state->Reg[14] = temp - 4;
       break;
  }

  ARMul_SetR15(state,R15CCINTMODE | vector);
}
