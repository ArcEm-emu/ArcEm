/*  armcopro.c -- co-processor interface:  ARM6 Instruction Emulator.
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

#ifdef ARMUL_COPRO_SUPPORT
#include "armcopro.h"
#include "armemu.h"
#include "arch/cp15.h"
#include "arch/fastmap.h"

#include <assert.h>

/***************************************************************************\
*                            Dummy Co-processor                             *
\***************************************************************************/

static const ARMul_CoPro DummyCoPro = {
  NULL,            /* CPInit */
  NULL,            /* CPExit */
  ARMul_NoCoPro4R, /* LDC */
  ARMul_NoCoPro4W, /* STC */
  ARMul_NoCoPro4W, /* MRC */
  ARMul_NoCoPro4R, /* MCR */
  ARMul_NoCoPro3R, /* CDP */
  NULL,            /* CPRead */
  NULL             /* CPWrite */
};

/***************************************************************************\
*                Define Co-Processor instruction handlers here              *
\***************************************************************************/


/***************************************************************************\
*         Install co-processor instruction handlers in this routine         *
\***************************************************************************/

bool ARMul_CoProInit(ARMul_State *state) {
  unsigned int i;

  /* initialise them all first */
  for (i = 0; i < 16; i++) {
    ARMul_CoProDetach(state, i);
  }

  /* Install CoPro Instruction handlers here */

    /* Add in the ARM3 processor CPU control coprocessor if the user
       wants it */
    if(state->HasCP15) {
      ARM3_CoProAttach(state);
    }

    /* No handlers below here */

    for (i = 0; i < 16; i++) {
      /* Call all the initialisation routines */
     if (state->CoPro[i]->CPInit) {
       (state->CoPro[i]->CPInit)(state);
     }
   }
   return true;
}


/***************************************************************************\
*         Install co-processor finalisation routines in this routine        *
\***************************************************************************/

void ARMul_CoProExit(ARMul_State *state) {
  unsigned int i;

  for (i = 0; i < 16; i++) {
    if (state->CoPro[i] && state->CoPro[i]->CPExit)
      (state->CoPro[i]->CPExit)(state);
    state->CoPro[i] = NULL;
  }
}

/***************************************************************************\
*              Routines to hook Co-processors into ARMulator                 *
\***************************************************************************/

void ARMul_CoProAttach(ARMul_State *state, unsigned number,
                       const ARMul_CoPro *copro)
{
 assert(copro);
 assert(copro->LDC);
 assert(copro->STC);
 assert(copro->MRC);
 assert(copro->MCR);
 assert(copro->CDP);

 state->CoPro[number] = copro;
}

void ARMul_CoProDetach(ARMul_State *state, unsigned number)
{
 ARMul_CoProAttach(state, number, &DummyCoPro);
}

/***************************************************************************\
*         There is no CoPro around, so Undefined Instruction trap           *
\***************************************************************************/

unsigned ARMul_NoCoPro3R(ARMul_State *state, unsigned a, ARMword b)
{
  UNUSED_VAR(state);
  UNUSED_VAR(a);
  UNUSED_VAR(b);

  return(ARMul_CANT);
}

unsigned ARMul_NoCoPro4R(ARMul_State *state, unsigned a, ARMword b, ARMword c)
{
  UNUSED_VAR(state);
  UNUSED_VAR(a);
  UNUSED_VAR(b);
  UNUSED_VAR(c);

  return(ARMul_CANT);
}

unsigned ARMul_NoCoPro4W(ARMul_State *state, unsigned a, ARMword b, ARMword *c)
{
  UNUSED_VAR(state);
  UNUSED_VAR(a);
  UNUSED_VAR(b);
  UNUSED_VAR(c);

  return(ARMul_CANT);
}

/***************************************************************************\
*           Return true if an interrupt is pending, false otherwise.        *
\***************************************************************************/

static inline bool IntPending(ARMul_State *state)
{
 ARMword excep = state->Exception & ~state->Reg[15];
 if(!excep) { /* anything? */
   return false;
 } else if(excep & Exception_FIQ) { /* FIQ? */
   ARMul_Abort(state,ARMul_FIQV);
   return true;
 } else { /* Must be IRQ */
   ARMul_Abort(state,ARMul_IRQV);
   return true;
 }
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
 cpab = (state->CoPro[CPNum]->LDC)(state,ARMul_FIRST,instr,0);
 while (cpab == ARMul_BUSY) {
    ARMul_Icycles(state,1);
    if (IntPending(state)) {
       cpab = (state->CoPro[CPNum]->LDC)(state,ARMul_INTERRUPT,instr,0);
       return;
       }
    else
       cpab = (state->CoPro[CPNum]->LDC)(state,ARMul_BUSY,instr,0);
    }
 if (cpab == ARMul_CANT) {
    CPTAKEABORT;
    return;
    }
 cpab = (state->CoPro[CPNum]->LDC)(state,ARMul_TRANSFER,instr,0);
 data = ARMul_LoadWordN(state,address);
 BUSUSEDINCPCN;
 if (BIT(21))
    LSBase = state->Base;
 cpab = (state->CoPro[CPNum]->LDC)(state,ARMul_DATA,instr,data);
 while (cpab == ARMul_INC) {
    address += 4;
    data = ARMul_LoadWordN(state,address);
    cpab = (state->CoPro[CPNum]->LDC)(state,ARMul_DATA,instr,data);
    }
 if (state->abortSig || state->Aborted) {
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
 cpab = (state->CoPro[CPNum]->STC)(state,ARMul_FIRST,instr,&data);
 while (cpab == ARMul_BUSY) {
    ARMul_Icycles(state,1);
    if (IntPending(state)) {
       cpab = (state->CoPro[CPNum]->STC)(state,ARMul_INTERRUPT,instr,0);
       return;
       }
    else
       cpab = (state->CoPro[CPNum]->STC)(state,ARMul_BUSY,instr,&data);
    }
 if (cpab == ARMul_CANT) {
    CPTAKEABORT;
    return;
    }
 if (ADDREXCEPT(address) ) {
    INTERNALABORT(address);
    }
 BUSUSEDINCPCN;
 if (BIT(21))
    LSBase = state->Base;
 cpab = (state->CoPro[CPNum]->STC)(state,ARMul_DATA,instr,&data);
 ARMul_StoreWordN(state,address,data);
 while (cpab == ARMul_INC) {
    address += 4;
    cpab = (state->CoPro[CPNum]->STC)(state,ARMul_DATA,instr,&data);
    ARMul_StoreWordN(state,address,data);
    }
 if (state->abortSig || state->Aborted) {
    TAKEABORT;
    }
 }

/***************************************************************************\
*        This function does the Busy-Waiting for an MCR instruction.        *
\***************************************************************************/

void ARMul_MCR(ARMul_State *state,ARMword instr, ARMword source)
{unsigned cpab;

 cpab = (state->CoPro[CPNum]->MCR)(state,ARMul_FIRST,instr,source);
 while (cpab == ARMul_BUSY) {
    ARMul_Icycles(state,1);
    if (IntPending(state)) {
       cpab = (state->CoPro[CPNum]->MCR)(state,ARMul_INTERRUPT,instr,0);
       return;
       }
    else
       cpab = (state->CoPro[CPNum]->MCR)(state,ARMul_BUSY,instr,source);
    }
 if (cpab == ARMul_CANT)
    ARMul_Abort(state,ARMul_UndefinedInstrV);
 else {
    BUSUSEDINCPCN;
    ARMul_Icycles(state,1); /* Should be C */
    }
 }

/***************************************************************************\
*        This function does the Busy-Waiting for an MRC instruction.        *
\***************************************************************************/

bool ARMul_MRC(ARMul_State *state,ARMword instr,ARMword *result)
{unsigned cpab;

 cpab = (state->CoPro[CPNum]->MRC)(state,ARMul_FIRST,instr,result);
 while (cpab == ARMul_BUSY) {
    ARMul_Icycles(state,1);
    if (IntPending(state)) {
       cpab = (state->CoPro[CPNum]->MRC)(state,ARMul_INTERRUPT,instr,0);
       return false;
       }
    else
       cpab = (state->CoPro[CPNum]->MRC)(state,ARMul_BUSY,instr,result);
    }
 if (cpab == ARMul_CANT) {
    ARMul_Abort(state,ARMul_UndefinedInstrV);
    return false;
    }
 else {
    BUSUSEDINCPCN;
    ARMul_Icycles(state,1);
    ARMul_Icycles(state,1);
    }
 return true;
}

/***************************************************************************\
*        This function does the Busy-Waiting for an CDP instruction.        *
\***************************************************************************/

void ARMul_CDP(ARMul_State *state,ARMword instr)
{unsigned cpab;

 cpab = (state->CoPro[CPNum]->CDP)(state,ARMul_FIRST,instr);
 while (cpab == ARMul_BUSY) {
    ARMul_Icycles(state,1);
    if (IntPending(state)) {
       cpab = (state->CoPro[CPNum]->CDP)(state,ARMul_INTERRUPT,instr);
       return;
       }
    else
       cpab = (state->CoPro[CPNum]->CDP)(state,ARMul_BUSY,instr);
    }
 if (cpab == ARMul_CANT)
    ARMul_Abort(state,ARMul_UndefinedInstrV);
 else
    BUSUSEDN;
}

#endif
