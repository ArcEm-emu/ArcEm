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
#include "hostfs.h"

FastMapEntry FastMap[FASTMAP_SIZE];

/***************************************************************************\
*                 Definitions for the emulator architecture                 *
\***************************************************************************/

uint_fast8_t ARMul_MultTable[32] = { 1,  2,  2,  3,  3,  4,  4,  5,
                                     5,  6,  6,  7,  7,  8,  8,  9,
                                     9, 10, 10, 11, 11, 12, 12, 13,
                                    13, 14, 14, 15, 15, 16, 16, 16};

#ifdef ARMUL_USE_IMMEDTABLE
ARMword ARMul_ImmedTable[4096]; /* immediate DP LHS values */
#endif
uint_fast8_t ARMul_BitList[256]; /* number of bits in a byte table */

uint_fast16_t ARMul_CCTable[16];

/***************************************************************************\
*         Call this routine once to set up the emulator's tables.           *
\***************************************************************************/

void ARMul_EmulateInit(void) {
  unsigned int i, j;

#ifdef ARMUL_USE_IMMEDTABLE
  for (i = 0; i < 4096; i++) { /* the values of 12 bit dp rhs's */
    ARMul_ImmedTable[i] = ROTATER(i & 0xffL,(i >> 7L) & 0x1eL);
  }
#endif

  for (i = 0; i < 256; ARMul_BitList[i++] = 0 ); /* how many bits in LSM */
  for (j = 1; j < 256; j <<= 1)
    for (i = 0; i < 256; i++)
      if ((i & j) > 0 )
         ARMul_BitList[i]++;

  for (i = 0; i < 256; i++)
    ARMul_BitList[i] *= 4; /* you always need 4 times these values */

#define V ((i&1)!=0)
#define C ((i&2)!=0)
#define Z ((i&4)!=0)
#define N ((i&8)!=0)
#define COMPUTE(CC,TST) ARMul_CCTable[CC] = 0; for(i=0;i<16;i++) if(TST) ARMul_CCTable[CC] |= 1<<i;
  COMPUTE(EQ,Z)
  COMPUTE(NE,!Z)
  COMPUTE(CS,C)
  COMPUTE(CC,!C)
  COMPUTE(MI,N)
  COMPUTE(PL,!N)
  COMPUTE(VS,V)
  COMPUTE(VC,!V)
  COMPUTE(HI,C&&!Z)
  COMPUTE(LS,!C||Z)
  COMPUTE(GE,N==V)
  COMPUTE(LT,N!=V)
  COMPUTE(GT,!Z&&(N==V))
  COMPUTE(LE,Z||(N!=V))
  COMPUTE(AL,1)
  COMPUTE(NV,0)
#undef V
#undef C
#undef Z
#undef N
#undef COMPUTE
}


/***************************************************************************\
*            Returns a new instantiation of the ARMulator's state           *
\***************************************************************************/

ARMul_State *ARMul_NewState(void)
{ARMul_State *state;
 unsigned i, j;

 state = state_alloc(sizeof(ARMul_State));

 for (i = 0; i < 16; i++) {
    state->Reg[i] = 0;
    for (j = 0; j < 4; j++)
       state->RegBank[j][i] = 0;
    }

 state->Aborted = FALSE;
 state->Display = NULL;
 state->FastMap = FastMap;
 
 ARMul_Reset(state);
 EventQ_Init(state);
 return(state);
 }

/***************************************************************************\
* Call this routine to set up the initial machine state (or perform a RESET *
\***************************************************************************/

void ARMul_Reset(ARMul_State *state)
{state->NextInstr = 0;
    state->Reg[15] = R15INTBITS | SVC26MODE;
 ARMul_R15Altered(state);
 state->Bank = SVCBANK;
 FLUSHPIPE;

 state->Exception = 0;
 state->NtransSig = (R15MODE) ? HIGH : LOW;
 state->abortSig = LOW;
 state->AbortAddr = 1;

 state->NumCycles = 0;
}


ARMword ARMul_DoProg(ARMul_State *state) {
  ARMword pc = 0;

  ARMul_Emulate26(state);
  return(pc);
}

/***************************************************************************\
* This routine causes an Abort to occur, including selecting the correct    *
* mode, register bank, and the saving of registers.  Call with the          *
* appropriate vector's memory address (0,4,8 ....)                          *
\***************************************************************************/

void ARMul_Abort(ARMul_State *state, ARMword vector) {
  ARMword temp;
  int exit_code;
  state->Aborted = FALSE;

#ifdef DEBUG
 printf("ARMul_Abort: vector=0x%x\n",vector);
#endif

  temp = state->Reg[15];

  switch (vector) {
    case ARMul_ResetV : /* RESET */
       SETABORT(R15INTBITS,SVC26MODE);
       ARMul_R15Altered(state);
       state->Reg[14] = temp;
       break;

    case ARMul_UndefinedInstrV : /* Undefined Instruction */
       SETABORT(R15IBIT,SVC26MODE);
       ARMul_R15Altered(state);
       state->Reg[14] = temp - 4;
       /*fprintf(stderr,"DAG: In ARMul_Abort: Taking undefined instruction trap R[14] being set to: 0x%08x\n",
               (unsigned int)(state->Reg[14])); */
       break;

    case ARMul_SWIV: /* Software Interrupt */
       {
         ARMword addr = (state->Reg[15]-8) & R15PCBITS;
         FastMapEntry *entry = FastMap_GetEntryNoWrap(state,addr);
         FastMapRes res = FastMap_DecodeRead(entry,state->FastMapMode);
         ARMword instr;
         if(FASTMAP_RESULT_DIRECT(res))
         {
           instr = *(FastMap_Log2Phy(entry,addr));
         }
         else if(FASTMAP_RESULT_FUNC(res))
         {
           instr = FastMap_LoadFunc(entry,state,addr);
         }
         else
           instr = 0; /* This should never happen! */
         if ((instr & 0xfdffc0) == ARCEM_SWI_CHUNK) {
           switch (instr & 0x3f) {
           case ARCEM_SWI_SHUTDOWN-ARCEM_SWI_CHUNK:
             exit_code = state->Reg[0] & 0xff;
			 state_free(state);
             exit(exit_code);
             break;
#ifdef HOSTFS_SUPPORT
           case ARCEM_SWI_HOSTFS-ARCEM_SWI_CHUNK:
             hostfs(state);
             /* hostfs operation may have taken a while; update EmuRate to try and mitigate any audio buffering issues */
             EmuRate_Update(state);
             return;
#endif
           case ARCEM_SWI_DEBUG-ARCEM_SWI_CHUNK:
             fprintf(stderr, "r0 = %08x  r4 = %08x  r8  = %08x  r12 = %08x\n"
                             "r1 = %08x  r5 = %08x  r9  = %08x  sp  = %08x\n"
                             "r2 = %08x  r6 = %08x  r10 = %08x  lr  = %08x\n"
                             "r3 = %08x  r7 = %08x  r11 = %08x  pc  = %08x\n"
                           "\n",
               state->Reg[0], state->Reg[4], state->Reg[8], state->Reg[12],
               state->Reg[1], state->Reg[5], state->Reg[9], state->Reg[13],
               state->Reg[2], state->Reg[6], state->Reg[10], state->Reg[14],
               state->Reg[3], state->Reg[7], state->Reg[11], state->Reg[15]);
             {
               unsigned p;
   
               for (p = state->Reg[15]; p < state->Reg[15] + 16; p += 4) {
               }
             }
             break;
           }
         }
         SETABORT(R15IBIT,SVC26MODE);
         ARMul_R15Altered(state);
         state->Reg[14] = temp - 4;
       }
       break;

    case ARMul_PrefetchAbortV : /* Prefetch Abort */
       state->AbortAddr = 1;
       SETABORT(R15IBIT,SVC26MODE);
       ARMul_R15Altered(state);
       state->Reg[14] = temp - 4;
       break;

    case ARMul_DataAbortV : /* Data Abort */
       SETABORT(R15IBIT,SVC26MODE);
       ARMul_R15Altered(state);
       state->Reg[14] = temp - 4; /* the PC must have been incremented */
       break;

    case ARMul_AddrExceptnV : /* Address Exception */
       SETABORT(R15IBIT,SVC26MODE);
       ARMul_R15Altered(state);
       state->Reg[14] = temp - 4;
       break;

    case ARMul_IRQV : /* IRQ */
       SETABORT(R15IBIT,IRQ26MODE);
       ARMul_R15Altered(state);
       state->Reg[14] = temp - 4;
       break;

    case ARMul_FIQV : /* FIQ */
       SETABORT(R15INTBITS,FIQ26MODE);
       ARMul_R15Altered(state);
       state->Reg[14] = temp - 4;
       break;
  }

  ARMul_SetR15(state,R15CCINTMODE | vector);
}
