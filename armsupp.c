/* Modified by DAG to remove memory leak */

/*  armsupp.c -- ARMulator support code:  ARM6 Instruction Emulator.
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

#include "armdefs.h"
#include "armemu.h"

extern ARMword *armflags;

/***************************************************************************\
*                    Definitions for the support routines                   *
\***************************************************************************/

/*void ARMul_SetReg(ARMul_State *state, unsigned mode, unsigned reg, ARMword value);
ARMword ARMul_GetPC(ARMul_State *state);
ARMword ARMul_GetNextPC(ARMul_State *state);
void ARMul_SetPC(ARMul_State *state, ARMword value);
ARMword ARMul_GetR15(ARMul_State *state);
void ARMul_SetR15(ARMul_State *state, ARMword value);

ARMword ARMul_GetCPSR(ARMul_State *state);
void ARMul_SetCPSR(ARMul_State *state, ARMword value);
void ARMul_FixCPSR(ARMul_State *state, ARMword instr, ARMword rhs);
ARMword ARMul_GetSPSR(ARMul_State *state, ARMword mode);
void ARMul_SetSPSR(ARMul_State *state, ARMword mode, ARMword value);
void ARMul_FixSPSR(ARMul_State *state, ARMword instr, ARMword rhs);

void ARMul_CPSRAltered(ARMul_State *state);
void ARMul_R15Altered(ARMul_State *state);

ARMword ARMul_SwitchMode(ARMul_State *state,ARMword oldmode, ARMword newmode);
static ARMword ModeToBank(ARMul_State *state,ARMword mode);

unsigned ARMul_NthReg(ARMword instr, unsigned number);

void ARMul_LDC(ARMul_State *state,ARMword instr,ARMword address);
void ARMul_STC(ARMul_State *state,ARMword instr,ARMword address);
void ARMul_MCR(ARMul_State *state,ARMword instr, ARMword source);
ARMword ARMul_MRC(ARMul_State *state,ARMword instr);
void ARMul_CDP(ARMul_State *state,ARMword instr);
void ARMul_UndefInstr(ARMul_State *state,ARMword instr);
unsigned IntPending(ARMul_State *state);

ARMword ARMul_Align(ARMul_State *state, ARMword address, ARMword data);

void ARMul_InvokeEvent(void);
static void InvokeList(unsigned long from, unsigned long to);*/


/***************************************************************************\
* Given a processor mode, this routine returns the register bank that       *
* will be accessed in that mode.                                            *
\***************************************************************************/

static ARMword ModeToBank(ARMul_State *state, ARMword mode) {
  static ARMword bankofmode[] = { USERBANK,  FIQBANK,   IRQBANK,   SVCBANK,
                                  DUMMYBANK, DUMMYBANK, DUMMYBANK, DUMMYBANK,
                                  DUMMYBANK, DUMMYBANK, DUMMYBANK, DUMMYBANK,
                                  DUMMYBANK, DUMMYBANK, DUMMYBANK, DUMMYBANK,
                                  USERBANK,  FIQBANK,   IRQBANK,   SVCBANK,
                                  DUMMYBANK, DUMMYBANK, DUMMYBANK, DUMMYBANK,
                                  DUMMYBANK, DUMMYBANK, DUMMYBANK, DUMMYBANK
                                };

    return(bankofmode[mode]);
}

/***************************************************************************\
* This routine sets the value of a register for a mode.                     *
\***************************************************************************/

void ARMul_SetReg(ARMul_State *state, unsigned mode, unsigned reg, ARMword value)
{mode &= MODEBITS;
 if (mode != statestr.Mode)
    statestr.RegBank[ModeToBank(state,(ARMword)mode)][reg] = value;
 else
    statestr.Reg[reg] = value;
}

/***************************************************************************\
* This routine returns the value of the PC, mode independently.             *
\***************************************************************************/

ARMword ARMul_GetPC(ARMul_State *state)
{
    return(R15PC);
}

/***************************************************************************\
* This routine returns the value of the PC, mode independently.             *
\***************************************************************************/

ARMword ARMul_GetNextPC(ARMul_State *state)
{
    return((statestr.Reg[15] + 4) & R15PCBITS);
}

/***************************************************************************\
* This routine sets the value of the PC.                                    *
\***************************************************************************/

void ARMul_SetPC(ARMul_State *state, ARMword value)
{
  statestr.Reg[15] = R15CCINTMODE | (value & R15PCBITS);
 FLUSHPIPE;
}

/***************************************************************************\
* This routine returns the value of register 15, mode independently.        *
\***************************************************************************/

ARMword ARMul_GetR15(ARMul_State *state)
{
    return(R15PC | ECC | ER15INT | EMODE);
}

/***************************************************************************\
* This routine sets the value of Register 15.                               *
\***************************************************************************/

void ARMul_SetR15(ARMul_State *state, ARMword value)
{
  statestr.Reg[15] = value;
  ARMul_R15Altered(state);
 FLUSHPIPE;
}

/***************************************************************************\
* This routine returns the value of the CPSR                                *
\***************************************************************************/

ARMword ARMul_GetCPSR(ARMul_State *state)
{
 return(CPSR);
 }

/***************************************************************************\
* This routine sets the value of the CPSR                                   *
\***************************************************************************/

void ARMul_SetCPSR(ARMul_State *state, ARMword value)
{statestr.Cpsr = CPSR;
 SETPSR(statestr.Cpsr,value);
 ARMul_CPSRAltered(state);
 }

/***************************************************************************\
* This routine does all the nasty bits involved in a write to the CPSR,     *
* including updating the register bank, given a MSR instruction.                    *
\***************************************************************************/

void ARMul_FixCPSR(ARMul_State *state, ARMword instr, ARMword rhs)
{statestr.Cpsr = CPSR;
 if (statestr.Bank==USERBANK) { /* Only write flags in user mode */
    if (BIT(19)) {
       SETCC(statestr.Cpsr,rhs);
       }
    }
 else { /* Not a user mode */
    if (BITS(16,19)==9) SETPSR(statestr.Cpsr,rhs);
    else if (BIT(16)) SETINTMODE(statestr.Cpsr,rhs);
    else if (BIT(19)) SETCC(statestr.Cpsr,rhs);
    }
 ARMul_CPSRAltered(state);
 }

/***************************************************************************\
* Get an SPSR from the specified mode                                       *
\***************************************************************************/

ARMword ARMul_GetSPSR(ARMul_State *state, ARMword mode)
{ARMword bank = ModeToBank(state,mode & MODEBITS);
 if (bank == USERBANK || bank == DUMMYBANK)
    return(CPSR);
 else
    return(statestr.Spsr[bank]);
}

/***************************************************************************\
* This routine does a write to an SPSR                                      *
\***************************************************************************/

void ARMul_SetSPSR(ARMul_State *state, ARMword mode, ARMword value)
{ARMword bank = ModeToBank(state,mode & MODEBITS);
 if (bank != USERBANK && bank !=DUMMYBANK)
    statestr.Spsr[bank] = value;
}

/***************************************************************************\
* This routine does a write to the current SPSR, given an MSR instruction   *
\***************************************************************************/

void ARMul_FixSPSR(ARMul_State *state, ARMword instr, ARMword rhs)
{if (statestr.Bank != USERBANK && statestr.Bank !=DUMMYBANK) {
    if (BITS(16,19)==9) SETPSR(statestr.Spsr[statestr.Bank],rhs);
    else if (BIT(16)) SETINTMODE(statestr.Spsr[statestr.Bank],rhs);
    else if (BIT(19)) SETCC(statestr.Spsr[statestr.Bank],rhs);
    }
}

/***************************************************************************\
* This routine updates the state of the emulator after the Cpsr has been    *
* changed.  Both the processor flags and register bank are updated.         *
\***************************************************************************/

void ARMul_CPSRAltered(ARMul_State *state)
{ARMword oldmode;

 statestr.Cpsr &= (CCBITS | INTBITS | R15MODEBITS);
 oldmode = statestr.Mode;
 if (statestr.Mode != (statestr.Cpsr & MODEBITS)) {
    statestr.Mode = ARMul_SwitchMode(state,statestr.Mode,statestr.Cpsr & MODEBITS);
    statestr.NtransSig = (statestr.Mode & 3)?HIGH:LOW;
    }

 ASSIGNINT(statestr.Cpsr & INTBITS);
 ASSIGNN((statestr.Cpsr & NBIT) != 0);
 ASSIGNZ((statestr.Cpsr & ZBIT) != 0);
 ASSIGNC((statestr.Cpsr & CBIT) != 0);
 ASSIGNV((statestr.Cpsr & VBIT) != 0);

 statestr.Reg[15] = ECC | ER15INT | EMODE | R15PC;
}

/***************************************************************************\
* This routine updates the state of the emulator after register 15 has      *
* been changed.  Both the processor flags and register bank are updated.    *
* This routine should only be called from a 26 bit mode.                    *
\***************************************************************************/

void ARMul_R15Altered(ARMul_State *state)
{
 if (statestr.Mode != R15MODE) {
    statestr.Mode = ARMul_SwitchMode(state,statestr.Mode,R15MODE);
    statestr.NtransSig = (statestr.Mode & 3)?HIGH:LOW;
    }
 ASSIGNR15INT(R15INT);
 ASSIGNN((statestr.Reg[15] & NBIT) != 0);
 ASSIGNZ((statestr.Reg[15] & ZBIT) != 0);
 ASSIGNC((statestr.Reg[15] & CBIT) != 0);
 ASSIGNV((statestr.Reg[15] & VBIT) != 0);
}

/***************************************************************************\
* This routine controls the saving and restoring of registers across mode   *
* changes.  The regbank matrix is largely unused, only rows 13 and 14 are   *
* used across all modes, 8 to 14 are used for FIQ, all others use the USER  *
* column.  It's easier this way.  old and new parameter are modes numbers.  *
* Notice the side effect of changing the Bank variable.                     *
\***************************************************************************/

ARMword ARMul_SwitchMode(ARMul_State *state,ARMword oldmode, ARMword newmode)
{unsigned i;

 oldmode = ModeToBank(state,oldmode);
 statestr.Bank = ModeToBank(state,newmode);
 if (oldmode != statestr.Bank) { /* really need to do it */
    switch (oldmode) { /* save away the old registers */
       case USERBANK  :
       case IRQBANK   :
       case SVCBANK   : if (statestr.Bank == FIQBANK)
                           for (i = 8; i < 13; i++)
                              statestr.RegBank[USERBANK][i] = statestr.Reg[i];
                        statestr.RegBank[oldmode][13] = statestr.Reg[13];
                        statestr.RegBank[oldmode][14] = statestr.Reg[14];
                        break;
       case FIQBANK   : for (i = 8; i < 15; i++)
                           statestr.RegBank[FIQBANK][i] = statestr.Reg[i];
                        break;
       case DUMMYBANK : for (i = 8; i < 15; i++)
                           statestr.RegBank[DUMMYBANK][i] = 0;
                        break;

       }
    switch (statestr.Bank) { /* restore the new registers */
       case USERBANK  :
       case IRQBANK   :
       case SVCBANK   : if (oldmode == FIQBANK)
                           for (i = 8; i < 13; i++)
                              statestr.Reg[i] = statestr.RegBank[USERBANK][i];
                        statestr.Reg[13] = statestr.RegBank[statestr.Bank][13];
                        statestr.Reg[14] = statestr.RegBank[statestr.Bank][14];
                        break;
       case FIQBANK  : for (i = 8; i < 15; i++)
                           statestr.Reg[i] = statestr.RegBank[FIQBANK][i];
                        break;
       case DUMMYBANK : for (i = 8; i < 15; i++)
                           statestr.Reg[i] = 0;
                        break;
       } /* switch */
    } /* if */
    return(newmode);
}

/***************************************************************************\
* Returns the register number of the nth register in a reg list.            *
\***************************************************************************/

unsigned ARMul_NthReg(ARMword instr, unsigned number)
{unsigned bit, upto;

 for (bit = 0, upto = 0; upto <= number; bit++)
    if (BIT(bit)) upto++;
 return(bit - 1);
}

/***************************************************************************\
* This function does the work of generating the addresses used in an        *
* LDC instruction.  The code here is always post-indexed, it's up to the    *
* caller to get the input address correct and to handle base register       *
* modification. It also handles the Busy-Waiting.                           *
\***************************************************************************/

void ARMul_LDC(ARMul_State *state,ARMword instr,ARMword address)
{
 unsigned cpab;
 ARMword data;

 UNDEF_LSCPCBaseWb;
 if (ADDREXCEPT(address)) {
    INTERNALABORT(address);
    }
 cpab = (statestr.LDC[CPNum])(state,ARMul_FIRST,instr,0);
 while (cpab == ARMul_BUSY) {
    ARMul_Icycles(1);
    if (IntPending(state)) {
       cpab = (statestr.LDC[CPNum])(state,ARMul_INTERRUPT,instr,0);
       return;
       }
    else
       cpab = (statestr.LDC[CPNum])(state,ARMul_BUSY,instr,0);
    }
 if (cpab == ARMul_CANT) {
    CPTAKEABORT;
    return;
    }
 cpab = (statestr.LDC[CPNum])(state,ARMul_TRANSFER,instr,0);
 data = ARMul_LoadWordN(state,address);
 BUSUSEDINCPCN;
 if (BIT(21))
    LSBase = statestr.Base;
 cpab = (statestr.LDC[CPNum])(state,ARMul_DATA,instr,data);
 while (cpab == ARMul_INC) {
    address += 4;
    data = ARMul_LoadWordN(state,address);
    cpab = (statestr.LDC[CPNum])(state,ARMul_DATA,instr,data);
    }
 if (statestr.abortSig || statestr.Aborted) {
    TAKEABORT;
    }
 }

/***************************************************************************\
* This function does the work of generating the addresses used in an        *
* STC instruction.  The code here is always post-indexed, it's up to the    *
* caller to get the input address correct and to handle base register       *
* modification. It also handles the Busy-Waiting.                           *
\***************************************************************************/

void ARMul_STC(ARMul_State *state,ARMword instr,ARMword address)
{unsigned cpab;
 ARMword data;

 UNDEF_LSCPCBaseWb;
 if (ADDREXCEPT(address)) {
    INTERNALABORT(address);
    }
 cpab = (statestr.STC[CPNum])(state,ARMul_FIRST,instr,&data);
 while (cpab == ARMul_BUSY) {
    ARMul_Icycles(1);
    if (IntPending(state)) {
       cpab = (statestr.STC[CPNum])(state,ARMul_INTERRUPT,instr,0);
       return;
       }
    else
       cpab = (statestr.STC[CPNum])(state,ARMul_BUSY,instr,&data);
    }
 if (cpab == ARMul_CANT) {
    CPTAKEABORT;
    return;
    }
#ifndef MODE32
 if (ADDREXCEPT(address) ) {
    INTERNALABORT(address);
    }
#endif
 BUSUSEDINCPCN;
 if (BIT(21))
    LSBase = statestr.Base;
 cpab = (statestr.STC[CPNum])(state,ARMul_DATA,instr,&data);
 ARMul_StoreWordN(state,address,data);
 while (cpab == ARMul_INC) {
    address += 4;
    cpab = (statestr.STC[CPNum])(state,ARMul_DATA,instr,&data);
    ARMul_StoreWordN(state,address,data);
    }
 if (statestr.abortSig || statestr.Aborted) {
    TAKEABORT;
    }
 }

/***************************************************************************\
*        This function does the Busy-Waiting for an MCR instruction.        *
\***************************************************************************/

void ARMul_MCR(ARMul_State *state,ARMword instr, ARMword source)
{unsigned cpab;

 cpab = (statestr.MCR[CPNum])(state,ARMul_FIRST,instr,source);
 while (cpab == ARMul_BUSY) {
    ARMul_Icycles(1);
    if (IntPending(state)) {
       cpab = (statestr.MCR[CPNum])(state,ARMul_INTERRUPT,instr,0);
       return;
       }
    else
       cpab = (statestr.MCR[CPNum])(state,ARMul_BUSY,instr,source);
    }
 if (cpab == ARMul_CANT)
    ARMul_Abort(state,ARMul_UndefinedInstrV);
 else {
    BUSUSEDINCPCN;
    ARMul_Icycles(1); /* Should be C */
    }
 }

/***************************************************************************\
*        This function does the Busy-Waiting for an MRC instruction.        *
\***************************************************************************/

ARMword ARMul_MRC(ARMul_State *state,ARMword instr)
{unsigned cpab;
 ARMword result = 0;

 cpab = (statestr.MRC[CPNum])(state,ARMul_FIRST,instr,&result);
 while (cpab == ARMul_BUSY) {
    ARMul_Icycles(1);
    if (IntPending(state)) {
       cpab = (statestr.MRC[CPNum])(state,ARMul_INTERRUPT,instr,0);
       return(0);
       }
    else
       cpab = (statestr.MRC[CPNum])(state,ARMul_BUSY,instr,&result);
    }
 if (cpab == ARMul_CANT) {
    ARMul_Abort(state,ARMul_UndefinedInstrV);
    result = ECC; /* Parent will destroy the flags otherwise */
    }
 else {
    BUSUSEDINCPCN;
    ARMul_Icycles(1);
    ARMul_Icycles(1);
    }
 return(result);
}

/***************************************************************************\
*        This function does the Busy-Waiting for an CDP instruction.        *
\***************************************************************************/

void ARMul_CDP(ARMul_State *state,ARMword instr)
{unsigned cpab;

 cpab = (statestr.CDP[CPNum])(state,ARMul_FIRST,instr);
 while (cpab == ARMul_BUSY) {
    ARMul_Icycles(1);
    if (IntPending(state)) {
       cpab = (statestr.CDP[CPNum])(state,ARMul_INTERRUPT,instr);
       return;
       }
    else
       cpab = (statestr.CDP[CPNum])(state,ARMul_BUSY,instr);
    }
 if (cpab == ARMul_CANT)
    ARMul_Abort(state,ARMul_UndefinedInstrV);
 else
    BUSUSEDN;
}

/***************************************************************************\
*      This function handles Undefined instructions, as CP isntruction      *
\***************************************************************************/

void ARMul_UndefInstr(ARMul_State *state,ARMword instr)
{
 ARMul_Abort(state,ARMul_UndefinedInstrV);
}

/***************************************************************************\
*           Return TRUE if an interrupt is pending, FALSE otherwise.        *
\***************************************************************************/

unsigned IntPending(ARMul_State *state)
{
 if (statestr.Exception) { /* Any exceptions */
   if ((statestr.Exception & 2) && !FFLAG) {
     ARMul_Abort(&statestr,ARMul_FIQV);
     return(TRUE);
   }
   else if ((statestr.Exception & 1) && !IFLAG) {
     ARMul_Abort(&statestr,ARMul_IRQV);
     return(TRUE);
   }
 }
 return(FALSE);
}

/***************************************************************************\
*               Align a word access to a non word boundary                  *
\***************************************************************************/

ARMword ARMul_Align(ARMul_State *state, ARMword address, ARMword data)
{/* this code assumes the address is really unaligned,
    as a shift by 32 is undefined in C */

 address = (address & 3) << 3; /* get the word address */
 return( ( data >> address) | (data << (32 - address)) ); /* rot right */
}


/***************************************************************************\
* This routine is used to call another routine after a certain number of    *
* cycles have been executed. The first parameter is the number of cycles    *
* delay before the function is called, the second argument is a pointer     *
* to the function. A delay of zero doesn't work, just call the function.    *
\***************************************************************************/

void ARMul_ScheduleEvent(struct EventNode* event, unsigned long delay, unsigned (*what)(void))
{
  statestr.Now = ARMul_Time + delay;
  event->func = what;
  event->next = *(statestr.EventPtr);
  *(statestr.EventPtr) = event;
}

