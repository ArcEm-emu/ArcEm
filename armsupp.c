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
#include "arch/fastmap.h"

/***************************************************************************\
* Given a processor mode, this routine returns the register bank that       *
* will be accessed in that mode.                                            *
\***************************************************************************/

static inline ARMword ModeToBank(ARMword mode) {
    return(mode&3);
}

/***************************************************************************\
* This routine sets the value of a register for a mode.                     *
\***************************************************************************/

void ARMul_SetReg(ARMul_State *state, unsigned mode, unsigned reg, ARMword value)
{mode &= R15MODEBITS;
 if (mode != R15MODE)
    state->RegBank[ModeToBank((ARMword)mode)][reg] = value;
 else
    state->Reg[reg] = value;
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
    return((state->Reg[15] + 4) & R15PCBITS);
}

/***************************************************************************\
* This routine sets the value of the PC.                                    *
\***************************************************************************/

void ARMul_SetPC(ARMul_State *state, ARMword value)
{
  state->Reg[15] = R15CCINTMODE | (value & R15PCBITS);
 FLUSHPIPE;
}

/***************************************************************************\
* This routine returns the value of register 15, mode independently.        *
\***************************************************************************/

ARMword ARMul_GetR15(ARMul_State *state)
{
    return state->Reg[15];
}

/***************************************************************************\
* This routine sets the value of Register 15.                               *
\***************************************************************************/

void ARMul_SetR15(ARMul_State *state, ARMword value)
{
  state->Reg[15] = value;
  ARMul_R15Altered(state);
 FLUSHPIPE;
}

/***************************************************************************\
* This routine updates the state of the emulator after register 15 has      *
* been changed.  Both the processor flags and register bank are updated.    *
* This routine should only be called from a 26 bit mode.                    *
\***************************************************************************/

void ARMul_R15Altered(ARMul_State *state)
{
 register ARMword mode = R15MODE;
 if (state->Bank != mode) {
    ARMul_SwitchMode(state,state->Bank,mode);
    state->NtransSig = (mode)?HIGH:LOW;
    FastMap_RebuildMapMode(state);
    }
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

 oldmode = ModeToBank(oldmode);
 state->Bank = ModeToBank(newmode);
 if (oldmode != state->Bank) { /* really need to do it */
    switch (oldmode) { /* save away the old registers */
       case USERBANK  :
       case IRQBANK   :
       case SVCBANK   : if (state->Bank == FIQBANK)
                           for (i = 8; i < 13; i++)
                              state->RegBank[USERBANK][i] = state->Reg[i];
                        state->RegBank[oldmode][13] = state->Reg[13];
                        state->RegBank[oldmode][14] = state->Reg[14];
                        break;
       case FIQBANK   : for (i = 8; i < 15; i++)
                           state->RegBank[FIQBANK][i] = state->Reg[i];
                        break;

       }
    switch (state->Bank) { /* restore the new registers */
       case USERBANK  :
       case IRQBANK   :
       case SVCBANK   : if (oldmode == FIQBANK)
                           for (i = 8; i < 13; i++)
                              state->Reg[i] = state->RegBank[USERBANK][i];
                        state->Reg[13] = state->RegBank[state->Bank][13];
                        state->Reg[14] = state->RegBank[state->Bank][14];
                        break;
       case FIQBANK  : for (i = 8; i < 15; i++)
                           state->Reg[i] = state->RegBank[FIQBANK][i];
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

#ifndef FASTMAP_INLINE
#include "arch/fastmap.c"
#endif
