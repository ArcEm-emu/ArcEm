/*  armemu.c -- Main instruction emulation:  ARM6 Instruction Emulator.
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

    /* This copy incorporates a number of bug fixes by David Alan Gilbert
    (arcem@treblig.org) */
#include "armdefs.h"
#include "armemu.h"

ARMul_State statestr;

/* global used to terminate the emulator */
static int kill_emulator;

typedef struct {
  ARMword instr;
  ARMEmuFunc func;
} PipelineEntry;

extern PipelineEntry abortpipe;

/***************************************************************************\
*                   Load Instruction                                        *
\***************************************************************************/
static void
ARMul_LoadInstr(ARMul_State *state,ARMword addr, PipelineEntry *p)
{
  state->NumCycles++;
  addr &= 0x3fffffc;

  ARMul_CLEARABORT;
  
  FastMapEntry *entry = FastMap_GetEntryNoWrap(addr);
  FastMapRes res = FastMap_DecodeRead(entry,MEMC.FastMapMode);
//  fprintf(stderr,"LoadInstr: %08x maps to entry %08x res %08x (mode %08x pc %08x)\n",addr,entry,res,MEMC.FastMapMode,state->Reg[15]);
  if(FASTMAP_RESULT_DIRECT(res))
  {
    ARMword *data = FastMap_Log2Phy(entry,addr);
    ARMword instr = p->instr = *data;
    ARMEmuFunc *pfunc = FastMap_Phy2Func(data);
    ARMEmuFunc temp = *pfunc;
    if(temp == FASTMAP_CLOBBEREDFUNC)
    {
      /* Decode the instruction */
      temp = *pfunc = ARMul_Emulate_DecodeInstr(instr);
    }
#if 0
    else if(temp != ARMul_Emulate_DecodeInstr(instr))
    {
      fprintf(stderr,"LoadInstr: %08x maps to entry %08x res %08x (mode %08x pc %08x)\n",addr,entry,res,MEMC.FastMapMode,state->Reg[15]);
      fprintf(stderr,"-> data %08x pfunc %08x instr %08x func %08x using ofs %08x\n",data,pfunc,instr,temp,MEMC.FastMapInstrFuncOfs);
      fprintf(stderr,"But should be %08x!\n",ARMul_Emulate_DecodeInstr(instr));
      exit(5);
    }
#endif
    p->func = temp;
  }
  else if(FASTMAP_RESULT_FUNC(res))
  {
    /* Use function, means we can't write back the decode result */
    ARMword instr = p->instr = FastMap_LoadFunc(entry,state,addr);
    p->func = ARMul_Emulate_DecodeInstr(instr);
  }
  else
  {
    /* Abort! */
    *p = abortpipe;
    ARMul_PREFETCHABORT(addr);
  }
}

/* --------------------------------------------------------------------------- */
static void
ARMul_LoadInstrTriplet(ARMul_State *state,ARMword addr,PipelineEntry *p)
{
  if ((addr << 20) > 0xff000000) {
    ARMul_LoadInstr(state,addr,p);
    ARMul_LoadInstr(state,addr+4,p+1);
    ARMul_LoadInstr(state,addr+8,p+2);
    return;
  }

  state->NumCycles += 3;
  addr &= 0x3fffffc;

  ARMul_CLEARABORT;
  
  FastMapEntry *entry = FastMap_GetEntryNoWrap(addr);
  FastMapRes res = FastMap_DecodeRead(entry,MEMC.FastMapMode);
  if(FASTMAP_RESULT_DIRECT(res))
  {
    ARMword *data = FastMap_Log2Phy(entry,addr);
    ARMEmuFunc *pfunc = FastMap_Phy2Func(data);
    int i;
    for(i=0;i<3;i++)
    {
      ARMword instr = p->instr = *data;
      ARMEmuFunc temp = *pfunc;
      if(temp == FASTMAP_CLOBBEREDFUNC)
      {
        /* Decode the instruction */
        temp = *pfunc = ARMul_Emulate_DecodeInstr(instr);
      }
      p->func = temp;
      data++;
      pfunc++;
      p++;
    }
  }
  else if(FASTMAP_RESULT_FUNC(res))
  {
    /* We would use the function here... except calling the function may alter the mapping (e.g. during first remapped ROM read following a reset)
       So instead we'll just fall back to calling LoadInstr 3 times, since this isn't a very common case anyway */
    state->NumCycles -= 3;
    ARMul_LoadInstr(state,addr,p);
    ARMul_LoadInstr(state,addr+4,p+1);
    ARMul_LoadInstr(state,addr+8,p+2);    
  }
  else
  {
    /* Abort! */
    int i;
    for(i=0;i<3;i++)
    {
      *p = abortpipe;
      p++;
    }
    ARMul_PREFETCHABORT(addr);
  }
}

/***************************************************************************\
* This routine is called at the beginning of every cycle, to invoke         *
* scheduled events.                                                         *
\***************************************************************************/

static void ARMul_InvokeEvent(ARMul_State *state)
{
  if (state->Now < ARMul_Time) {
    DisplayKbd_Poll(state);
  }
}


void ARMul_Icycles(ARMul_State *state,unsigned number)
{
  state->NumCycles += number;
  ARMul_CLEARABORT;
}


/***************************************************************************\
* Assigns the N and Z flags depending on the value of result                *
\***************************************************************************/

static void
ARMul_NegZero(ARMul_State *state, ARMword result)
{
  ASSIGNN(NEG(result));
  ASSIGNZ(result == 0);
}


/***************************************************************************\
* Assigns the C flag after an addition of a and b to give result            *
\***************************************************************************/

static void
ARMul_AddCarry(ARMul_State *state, ARMword a, ARMword b, ARMword result)
{
  ASSIGNC( (NEG(a) && (NEG(b) || POS(result))) ||
           (NEG(b) && POS(result)) );
}

/***************************************************************************\
* Assigns the V flag after an addition of a and b to give result            *
\***************************************************************************/

static void
ARMul_AddOverflow(ARMul_State *state, ARMword a, ARMword b, ARMword result)
{
  ASSIGNV( (NEG(a) && NEG(b) && POS(result)) ||
           (POS(a) && POS(b) && NEG(result)) );
}


/***************************************************************************\
* Assigns the C flag after an subtraction of a and b to give result         *
\***************************************************************************/

static void
ARMul_SubCarry(ARMul_State *state, ARMword a, ARMword b, ARMword result)
{
  ASSIGNC( (NEG(a) && POS(b)) ||
           (NEG(a) && POS(result)) ||
           (POS(b) && POS(result)) );
}

/***************************************************************************\
* Assigns the V flag after an subtraction of a and b to give result         *
\***************************************************************************/

static void
ARMul_SubOverflow(ARMul_State *state, ARMword a, ARMword b, ARMword result)
{
  ASSIGNV( (NEG(a) && POS(b) && POS(result)) ||
           (POS(a) && NEG(b) && NEG(result)) );
}

/***************************************************************************\
* This routine evaluates most Data Processing register RHSs with the S     *
* bit clear.  It is intended to be called from the macro DPRegRHS, which    *
* filters the common case of an unshifted register with in line code        *
\***************************************************************************/

#if !(defined(__riscos__) && defined(ENABLE_ASM))
#if 1
typedef ARMword (*RHSFunc)(ARMul_State *state,ARMword instr,ARMword base);

static ARMword RHSFunc_LSL_Imm(ARMul_State *state,ARMword instr,ARMword base)
{
  base = state->Reg[base];
  ARMword shamt = BITS(7,11);
  return base<<shamt;
}

static ARMword RHSFunc_LSR_Imm(ARMul_State *state,ARMword instr,ARMword base)
{
  base = state->Reg[base];
  ARMword shamt = BITS(7,11);
  return (shamt?base>>shamt:0);
}

static ARMword RHSFunc_ASR_Imm(ARMul_State *state,ARMword instr,ARMword base)
{
  base = state->Reg[base];
  ARMword shamt = BITS(7,11);
  return (shamt?((ARMword)((int)base>>(int)shamt)):((ARMword)((int)base>>31L)));
}

static ARMword RHSFunc_ROR_Imm(ARMul_State *state,ARMword instr,ARMword base)
{
  base = state->Reg[base];
  ARMword shamt = BITS(7,11);
  return (shamt?((base << (32 - shamt)) | (base >> shamt)):((base >> 1) | (CFLAG << 31)));
}

static ARMword RHSFunc_LSL_Reg(ARMul_State *state,ARMword instr,ARMword base)
{
    UNDEF_Shift;
    INCPC;
    base = state->Reg[base];
    ARMul_Icycles(state,1);
    ARMword shamt = state->Reg[BITS(8,11)] & 0xff;
  return (shamt>=32?0:base<<shamt);
}

static ARMword RHSFunc_LSR_Reg(ARMul_State *state,ARMword instr,ARMword base)
{
    UNDEF_Shift;
    INCPC;
    base = state->Reg[base];
    ARMul_Icycles(state,1);
    ARMword shamt = state->Reg[BITS(8,11)] & 0xff;
  return (shamt>=32?0:base>>shamt);
}

static ARMword RHSFunc_ASR_Reg(ARMul_State *state,ARMword instr,ARMword base)
{
    UNDEF_Shift;
    INCPC;
    base = state->Reg[base];
    ARMul_Icycles(state,1);
    ARMword shamt = state->Reg[BITS(8,11)] & 0xff;
  return (shamt<32?((ARMword)((int)base>>(int)shamt)):((ARMword)((int)base>>31L)));
}

static ARMword RHSFunc_ROR_Reg(ARMul_State *state,ARMword instr,ARMword base)
{
    UNDEF_Shift;
    INCPC;
    base = state->Reg[base];
    ARMul_Icycles(state,1);
    ARMword shamt = state->Reg[BITS(8,11)] & 0x1f;
  return ((base << (32 - shamt)) | (base >> shamt));
}

static const RHSFunc RHSFuncs[8] = {RHSFunc_LSL_Imm,RHSFunc_LSL_Reg,RHSFunc_LSR_Imm,RHSFunc_LSR_Reg,RHSFunc_ASR_Imm,RHSFunc_ASR_Reg,RHSFunc_ROR_Imm,RHSFunc_ROR_Reg};
static ARMword
GetDPRegRHS(ARMul_State *state, ARMword instr)
{
 return (RHSFuncs[BITS(4,6)])(state,instr,RHSReg);
 }
#else
static ARMword
GetDPRegRHS(ARMul_State *state, ARMword instr)
{
  ARMword shamt , base;

 base = RHSReg;
 if (BIT(4)) { /* shift amount in a register */
    UNDEF_Shift;
    INCPC;
    base = state->Reg[base];
    ARMul_Icycles(state,1);
    shamt = state->Reg[BITS(8,11)] & 0xff;
    switch ((int)BITS(5,6)) {
       case LSL: if (shamt == 0)
                     return(base);
                  else if (shamt >= 32)
                     return(0);
                  else
                     return(base << shamt);
       case LSR: if (shamt == 0)
                     return(base);
                  else if (shamt >= 32)
                     return(0);
                  else
                     return(base >> shamt);
       case ASR: if (shamt == 0)
                     return(base);
                  else if (shamt >= 32)
                     return((ARMword)((int)base >> 31L));
                  else
                     return((ARMword)((int)base >> (int)shamt));
       case ROR: shamt &= 0x1f;
                  if (shamt == 0)
                     return(base);
                  else
                     return((base << (32 - shamt)) | (base >> shamt));
       }
    }
 else { /* shift amount is a constant */
    base = state->Reg[base];
    shamt = BITS(7,11);
    switch ((int)BITS(5,6)) {
       case LSL: return(base<<shamt);
       case LSR: if (shamt == 0)
                     return(0);
                  else
                     return(base >> shamt);
       case ASR: if (shamt == 0)
                     return((ARMword)((int)base >> 31L));
                  else
                     return((ARMword)((int)base >> (int)shamt));
       case ROR: if (shamt==0) /* it's an RRX */
                     return((base >> 1) | (CFLAG << 31));
                  else
                     return((base << (32 - shamt)) | (base >> shamt));
       }
    }
 return(0); /* just to shut up lint */
 }
#endif
#endif

/***************************************************************************\
* This routine evaluates most Logical Data Processing register RHSs        *
* with the S bit set.  It is intended to be called from the macro           *
* DPSRegRHS, which filters the common case of an unshifted register         *
* with in line code                                                         *
\***************************************************************************/

#if !(defined(__riscos__) && defined(ENABLE_ASM))
static ARMword
GetDPSRegRHS(ARMul_State *state, ARMword instr)
{
  ARMword shamt , base;

 base = RHSReg;
 if (BIT(4)) { /* shift amount in a register */
    UNDEF_Shift;
    INCPC;
    base = state->Reg[base];
    ARMul_Icycles(state,1);
    shamt = state->Reg[BITS(8,11)] & 0xff;
    switch ((int)BITS(5,6)) {
       case LSL: if (shamt == 0)
                     return(base);
                  else if (shamt == 32) {
                     ASSIGNC(base & 1);
                     return(0);
                     }
                  else if (shamt > 32) {
                     CLEARC;
                     return(0);
                     }
                  else {
                     ASSIGNC((base >> (32-shamt)) & 1);
                     return(base << shamt);
                     }
       case LSR: if (shamt == 0)
                     return(base);
                  else if (shamt == 32) {
                     ASSIGNC(base >> 31);
                     return(0);
                     }
                  else if (shamt > 32) {
                     CLEARC;
                     return(0);
                     }
                  else {
                     ASSIGNC((base >> (shamt - 1)) & 1);
                     return(base >> shamt);
                     }
       case ASR: if (shamt == 0)
                     return(base);
                  else if (shamt >= 32) {
                     ASSIGNC(base >> 31L);
                     return((ARMword)((int)base >> 31L));
                     }
                  else {
                     ASSIGNC((ARMword)((int)base >> (int)(shamt-1)) & 1);
                     return((ARMword)((int)base >> (int)shamt));
                     }
       case ROR: if (shamt == 0)
                     return(base);
                  shamt &= 0x1f;
                  if (shamt == 0) {
                     ASSIGNC(base >> 31);
                     return(base);
                     }
                  else {
                     ASSIGNC((base >> (shamt-1)) & 1);
                     return((base << (32-shamt)) | (base >> shamt));
                     }
       }
    }
 else { /* shift amount is a constant */
    base = state->Reg[base];
    shamt = BITS(7,11);
    switch ((int)BITS(5,6)) {
       case LSL:
                  /* BUGFIX: This copes with the case when base = R15 and shamt = 0 
                     from Patrick (Adapted Cat) */
                    if (shamt)
                      ASSIGNC((base >> (32-shamt)) & 1);
                  return(base << shamt);
       case LSR: if (shamt == 0) {
                     ASSIGNC(base >> 31);
                     return(0);
                     }
                  else {
                     ASSIGNC((base >> (shamt - 1)) & 1);
                     return(base >> shamt);
                     }
       case ASR: if (shamt == 0) {
                     ASSIGNC(base >> 31L);
                     return((ARMword)((int)base >> 31L));
                     }
                  else {
                     ASSIGNC((ARMword)((int)base >> (int)(shamt-1)) & 1);
                     return((ARMword)((int)base >> (int)shamt));
                     }
       case ROR: if (shamt == 0) { /* its an RRX */
                     shamt = CFLAG;
                     ASSIGNC(base & 1);
                     return((base >> 1) | (shamt << 31));
                     }
                  else {
                     ASSIGNC((base >> (shamt - 1)) & 1);
                     return((base << (32-shamt)) | (base >> shamt));
                     }
       }
    }
 return(0); /* just to shut up lint */
 }
#endif


/***************************************************************************\
* This routine handles writes to register 15 when the S bit is not set.     *
\***************************************************************************/

static void WriteR15(ARMul_State *state, ARMword src)
{
 SETPC(src);
 ARMul_R15Altered(state);
 FLUSHPIPE;
 }


/***************************************************************************\
* This routine handles writes to register 15 when the S bit is set.         *
\***************************************************************************/

static void WriteSR15(ARMul_State *state, ARMword src)
{
 if (state->Bank == USERBANK)
    state->Reg[15] = (src & (CCBITS | R15PCBITS)) | R15INTMODE;
 else
    state->Reg[15] = src;
 ARMul_R15Altered(state);
 FLUSHPIPE;
 }


/***************************************************************************\
* This routine evaluates most Load and Store register RHSs.  It is         *
* intended to be called from the macro LSRegRHS, which filters the          *
* common case of an unshifted register with in line code                    *
\***************************************************************************/

static ARMword GetLSRegRHS(ARMul_State *state, ARMword instr)
{ARMword shamt, base;

 base = RHSReg;
 base = state->Reg[base];

 shamt = BITS(7,11);
 switch ((int)BITS(5,6)) {
    case LSL: return(base << shamt);
    case LSR: if (shamt == 0)
                  return(0);
               else
                  return(base >> shamt);

    case ASR: if (shamt == 0)
                  return((ARMword)((int)base >> 31L));
               else
                  return((ARMword)((int)base >> (int)shamt));

    case ROR: if (shamt == 0) /* it's an RRX */
                  return((base >> 1) | (CFLAG << 31));
               else
                  return((base << (32-shamt)) | (base >> shamt));
    }
 return(0); /* just to shut up lint */
 }

/***************************************************************************\
* This function does the work of loading a word for a LDR instruction.      *
\***************************************************************************/

static unsigned LoadWord(ARMul_State *state, ARMword instr, ARMword address)
{
 ARMword dest;

 BUSUSEDINCPCS;
 if (ADDREXCEPT(address)) {
    INTERNALABORT(address);
    }
 dest = ARMul_LoadWordN(state,address);
 if (state->Aborted) {
    TAKEABORT;
    return LATEABTSIG;
    }
 if (address & 3)
    dest = ARMul_Align(state,address,dest);
 WRITEDEST(dest);
 ARMul_Icycles(state,1);

 return(DESTReg != LHSReg);
}

/***************************************************************************\
* This function does the work of loading a byte for a LDRB instruction.     *
\***************************************************************************/

static unsigned LoadByte(ARMul_State *state, ARMword instr, ARMword address)
{
 ARMword dest;

 BUSUSEDINCPCS;
 if (ADDREXCEPT(address)) {
    INTERNALABORT(address);
    }
 dest = ARMul_LoadByte(state,address);
 if (state->Aborted) {
    TAKEABORT;
    return LATEABTSIG;
    }
 UNDEF_LSRBPC;
 WRITEDEST(dest);
 ARMul_Icycles(state,1);
 return(DESTReg != LHSReg);
}

/***************************************************************************\
* This function does the work of storing a word from a STR instruction.     *
\***************************************************************************/

static unsigned StoreWord(ARMul_State *state, ARMword instr, ARMword address)
{BUSUSEDINCPCN;
 if (ADDREXCEPT(address)) {
    INTERNALABORT(address);
    (void)ARMul_LoadWordN(state,address);
    }
 else
    ARMul_StoreWordN(state,address,DEST);
 if (state->Aborted) {
    TAKEABORT;
    return LATEABTSIG;
    }
 return(TRUE);
}

/***************************************************************************\
* This function does the work of storing a byte for a STRB instruction.     *
\***************************************************************************/

static unsigned StoreByte(ARMul_State *state, ARMword instr, ARMword address)
{BUSUSEDINCPCN;
 if (ADDREXCEPT(address)) {
    INTERNALABORT(address);
    (void)ARMul_LoadByte(state,address);
    }
 else
    ARMul_StoreByte(state,address,DEST);
 if (state->Aborted) {
    TAKEABORT;
    return LATEABTSIG;
    }
 UNDEF_LSRBPC;
 return(TRUE);
}


/***************************************************************************\
* This function does the work of loading the registers listed in an LDM     *
* instruction, when the S bit is clear.  The code here is always increment  *
* after, it's up to the caller to get the input address correct and to      *
* handle base register modification.                                        *
\***************************************************************************/

static void LoadMult(ARMul_State *state, ARMword instr,
                     ARMword address, ARMword WBBase)
{ARMword dest, temp, temp2;

 UNDEF_LSMNoRegs;
 UNDEF_LSMPCBase;
 UNDEF_LSMBaseInListWb;
 BUSUSEDINCPCS;
 if (ADDREXCEPT(address)) {
    INTERNALABORT(address);
    }
 if (BIT(21) && LHSReg != 15)
    LSBase = WBBase;

    temp2 = state->Reg[15];

    /* Check if we can use the fastmap */
    if(((address<<20) <= 0xfc000000) && !state->Aborted)
    {
       FastMapEntry *entry = FastMap_GetEntry(address);
       FastMapRes res = FastMap_DecodeRead(entry,MEMC.FastMapMode);
       if(FASTMAP_RESULT_DIRECT(res))
       {
          /* Do it fast
             This assumes we don't differentiate between N & S cycles */
          ARMul_CLEARABORT;
          ARMword *data = FastMap_Log2Phy(entry,address&~3);
          ARMword count=0;
          for(temp=0;temp<16;temp++)
            if(BIT(temp))
            {
              state->Reg[temp] = *(data++);
              count++;
            }
          state->NumCycles += count;
          goto done;
       }
    }

    for (temp = 0; !BIT(temp); temp++); /* N cycle first */
    dest = ARMul_LoadWordN(state,address);
    if (!state->abortSig && !state->Aborted)
       state->Reg[temp] = dest;
    else
       if (!state->Aborted)
          state->Aborted = ARMul_DataAbortV;

    temp++;

    for (; temp < 16; temp++) /* S cycles from here on */
       if (BIT(temp)) { /* load this register */
          address += 4;
          dest = ARMul_LoadWordS(state,address);
          if (!state->abortSig && !state->Aborted)
             state->Reg[temp] = dest;
          else
             if (!state->Aborted)
                state->Aborted = ARMul_DataAbortV;
          }

done:
 if ((BIT(15)) && (!state->abortSig && !state->Aborted)) {
   /* PC is in the reg list */
    state->Reg[15] = (temp2 & (R15IFBITS | R15MODEBITS | CCBITS)) | (state->Reg[15] & ~(R15IFBITS | R15MODEBITS | CCBITS));
    FLUSHPIPE;
    }

 ARMul_Icycles(state,1); /* to write back the final register */

 if (state->Aborted) {
    if (BIT(21) && LHSReg != 15)
       LSBase = WBBase;
    TAKEABORT;
    }
 }

/***************************************************************************\
* This function does the work of loading the registers listed in an LDM     *
* instruction, when the S bit is set. The code here is always increment     *
* after, it's up to the caller to get the input address correct and to      *
* handle base register modification.                                        *
\***************************************************************************/

static void LoadSMult(ARMul_State *state, ARMword instr,
                      ARMword address, ARMword WBBase)
{ARMword dest, temp, temp2;

 UNDEF_LSMNoRegs;
 UNDEF_LSMPCBase;
 UNDEF_LSMBaseInListWb;
 BUSUSEDINCPCS;
 if (ADDREXCEPT(address)) {
    INTERNALABORT(address);
    }

 /* Actually do the write back (Hey guys - this is in after the mode change!) DAG */
 if (BIT(21) && LHSReg != 15)
    LSBase = WBBase;

    temp2 = state->Reg[15];

 if (!BIT(15) && state->Bank != USERBANK) {
    (void)ARMul_SwitchMode(state,temp2,USER26MODE); /* temporary reg bank switch */
    UNDEF_LSMUserBankWb;
    }

    /* Check if we can use the fastmap */
    if(((address<<20) <= 0xfc000000) && !state->Aborted)
    {
       FastMapEntry *entry = FastMap_GetEntry(address);
       FastMapRes res = FastMap_DecodeRead(entry,MEMC.FastMapMode);
       if(FASTMAP_RESULT_DIRECT(res))
       {
          /* Do it fast
             This assumes we don't differentiate between N & S cycles */
          ARMul_CLEARABORT;
          ARMword *data = FastMap_Log2Phy(entry,address&~3);
          ARMword count=0;
          for(temp=0;temp<16;temp++)
            if(BIT(temp))
            {
              state->Reg[temp] = *(data++);
              count++;
            }
          state->NumCycles += count;
          goto done;
       }
    }

    for (temp = 0; !BIT(temp); temp++); /* N cycle first */
    dest = ARMul_LoadWordN(state,address);
    if (!state->abortSig)
       state->Reg[temp] = dest;
    else
       if (!state->Aborted)
          state->Aborted = ARMul_DataAbortV;
    temp++;

    for (; temp < 16; temp++) /* S cycles from here on */
       if (BIT(temp)) { /* load this register */
          address += 4;
          dest = ARMul_LoadWordS(state,address);
          if (!(state->abortSig || state->Aborted))
             state->Reg[temp] = dest;
          else
             if (!state->Aborted)
                state->Aborted = ARMul_DataAbortV;
          }
done:
 /* DAG - stop corruption of R15 after an abort */
 if (!(state->abortSig || state->Aborted)) {
   if (BIT(15)) { /* PC is in the reg list */
      if ((temp2 & R15MODEBITS) == USER26MODE) { /* protect bits in user mode */
         state->Reg[15] = (temp2 & (R15IFBITS | R15MODEBITS)) | (state->Reg[15] & ~(R15IFBITS | R15MODEBITS));
         }
      else
         ARMul_R15Altered(state);
      FLUSHPIPE;
      }

 }
 if (!BIT(15) && ((temp2 & R15MODEBITS) != USER26MODE))
    (void)ARMul_SwitchMode(state,USER26MODE,temp2); /* restore the correct bank */
 ARMul_Icycles(state,1); /* to write back the final register */

 if (state->Aborted) {
    if (BIT(21) && LHSReg != 15)
       LSBase = WBBase;
    TAKEABORT;
    }

}

/***************************************************************************\
* This function does the work of storing the registers listed in an STM     *
* instruction, when the S bit is clear.  The code here is always increment  *
* after, it's up to the caller to get the input address correct and to      *
* handle base register modification.                                        *
\***************************************************************************/

static void StoreMult(ARMul_State *state, ARMword instr,
                      ARMword address, ARMword WBBase)
{ARMword temp;

 UNDEF_LSMNoRegs;
 UNDEF_LSMPCBase;
 UNDEF_LSMBaseInListWb;
 BUSUSEDINCPCN;
 if (ADDREXCEPT(address)) {
    INTERNALABORT(address);
    }

 for (temp = 0; !BIT(temp); temp++); /* N cycle first */

    /* Check if we can use the fastmap */
    if(((address<<20) <= 0xfc000000) && !state->Aborted)
    {
       FastMapEntry *entry = FastMap_GetEntry(address);
       FastMapRes res = FastMap_DecodeWrite(entry,MEMC.FastMapMode);
       if(FASTMAP_RESULT_DIRECT(res))
       {
          /* Do it fast
             This assumes we don't differentiate between N & S cycles */
          ARMul_CLEARABORT;
          ARMword *data = FastMap_Log2Phy(entry,address&~3);
          ARMEmuFunc *pfunc = FastMap_Phy2Func(data);
          ARMword count=1;
          *(data++) = state->Reg[temp++];
          *(pfunc++) = FASTMAP_CLOBBEREDFUNC;
          if (BIT(21) && LHSReg != 15)
             LSBase = WBBase;
          for(;temp<16;temp++)
            if(BIT(temp))
            {
              *(data++) = state->Reg[temp];
              *(pfunc++) = FASTMAP_CLOBBEREDFUNC;
              count++;
            }
          state->NumCycles += count;
          return;
       }
    }

 if (state->Aborted) {
    (void)ARMul_LoadWordN(state,address);
    for (; temp < 16; temp++) /* Fake the Stores as Loads */
       if (BIT(temp)) { /* save this register */
          address += 4;
          (void)ARMul_LoadWordS(state,address);
          }
    if (BIT(21) && LHSReg != 15)
       LSBase = WBBase;
    TAKEABORT;
    return;
    }
 else
    ARMul_StoreWordN(state,address,state->Reg[temp++]);
 if (state->abortSig && !state->Aborted)
    state->Aborted = ARMul_DataAbortV;

 if (BIT(21) && LHSReg != 15)
    LSBase = WBBase;

 for (; temp < 16; temp++) /* S cycles from here on */
    if (BIT(temp)) { /* save this register */
       address += 4;
       ARMul_StoreWordS(state,address,state->Reg[temp]);
       if (state->abortSig && !state->Aborted)
             state->Aborted = ARMul_DataAbortV;
       }
    if (state->Aborted) {
       TAKEABORT;
       }
 }

/***************************************************************************\
* This function does the work of storing the registers listed in an STM     *
* instruction when the S bit is set.  The code here is always increment     *
* after, it's up to the caller to get the input address correct and to      *
* handle base register modification.                                        *
\***************************************************************************/

static void StoreSMult(ARMul_State *state, ARMword instr,
                       ARMword address, ARMword WBBase)
{ARMword temp;

 UNDEF_LSMNoRegs;
 UNDEF_LSMPCBase;
 UNDEF_LSMBaseInListWb;
 BUSUSEDINCPCN;
 if (ADDREXCEPT(address)) {
    INTERNALABORT(address);
    }

 if (state->Bank != USERBANK) {
    (void)ARMul_SwitchMode(state,state->Reg[15],USER26MODE); /* Force User Bank */
    UNDEF_LSMUserBankWb;
    }

 for (temp = 0; !BIT(temp); temp++); /* N cycle first */

    /* Check if we can use the fastmap */
    if(((address<<20) <= 0xfc000000) && !state->Aborted)
    {
       FastMapEntry *entry = FastMap_GetEntry(address);
       FastMapRes res = FastMap_DecodeWrite(entry,MEMC.FastMapMode);
       if(FASTMAP_RESULT_DIRECT(res))
       {
          /* Do it fast
             This assumes we don't differentiate between N & S cycles */
          ARMul_CLEARABORT;
          ARMword *data = FastMap_Log2Phy(entry,address&~3);
          ARMEmuFunc *pfunc = FastMap_Phy2Func(data);
          ARMword count=1;
          *(data++) = state->Reg[temp++];
          *(pfunc++) = FASTMAP_CLOBBEREDFUNC;
          if (BIT(21) && LHSReg != 15)
             LSBase = WBBase;
          for(;temp<16;temp++)
            if(BIT(temp))
            {
              *(data++) = state->Reg[temp];
              *(pfunc++) = FASTMAP_CLOBBEREDFUNC;
              count++;
            }
          state->NumCycles += count;
          goto done;
       }
    }

 if (state->Aborted) {
    (void)ARMul_LoadWordN(state,address);
    for (; temp < 16; temp++) /* Fake the Stores as Loads */
       if (BIT(temp)) { /* save this register */
          address += 4;
          (void)ARMul_LoadWordS(state,address);
          }
    if (BIT(21) && LHSReg != 15)
       LSBase = WBBase;
    TAKEABORT;
    return;
    }
 else
    ARMul_StoreWordN(state,address,state->Reg[temp++]);
 if (state->abortSig && !state->Aborted)
    state->Aborted = ARMul_DataAbortV;

 if (BIT(21) && LHSReg != 15)
    LSBase = WBBase;

 for (; temp < 16; temp++) /* S cycles from here on */
    if (BIT(temp)) { /* save this register */
       address += 4;
       ARMul_StoreWordS(state,address,state->Reg[temp]);
       if (state->abortSig && !state->Aborted)
             state->Aborted = ARMul_DataAbortV;
       }

done:
 if (R15MODE != USER26MODE)
    (void)ARMul_SwitchMode(state,USER26MODE,state->Reg[15]); /* restore the correct bank */

 if (state->Aborted) {
    TAKEABORT;
    }
}


/***************************************************************************\
*                             EMULATION of ARM6                             *
\***************************************************************************/

#define EMFUNCDECL26(name) ARMul_Emulate26_ ## name
#define EMFUNC_CONDTEST
#include "armemuinstr.c"

#undef EMFUNCDECL26
#undef EMFUNC_CONDTEST

  int pipeidx = 0; /* Index of instruction to run */

/* ################################################################################## */
/* ## Function called when the decode is unknown                                   ## */
/* ################################################################################## */
ARMEmuFunc ARMul_Emulate_DecodeInstr(ARMword instr) {
  ARMEmuFunc f;
#define EMFUNCDECL26(name) ARMul_Emulate26_ ## name
#include "armemudec.c"

  return f;
} /* ARMul_Emulate_DecodeInstr */

/* Pipeline entry used for prefetch aborts */
PipelineEntry abortpipe = {
  ARMul_ABORTWORD,
  EMFUNCDECL26(SWI)
};

#define PIPESIZE 4 /* 3 or 4. 4 seems to be slightly faster? */

void
ARMul_Emulate26(ARMul_State *state)
{
  PipelineEntry pipe[PIPESIZE];   /* Instruction pipeline */
  ARMword pc = 0;          /* The address of the current instruction */

  /**************************************************************************\
   *                        Execute the next instruction                    *
  \**************************************************************************/
  kill_emulator = 0;
  while (kill_emulator == 0) {
    if (state->NextInstr < PRIMEPIPE) {
      pipe[1].instr = state->decoded;
      pipe[2].instr = state->loaded;
      pc            = state->pc;

      pipe[1].func = ARMul_Emulate_DecodeInstr(pipe[1].instr);
      pipe[2].func = ARMul_Emulate_DecodeInstr(pipe[2].instr);

      pipeidx = 0;
    }

    for (;;) { /* just keep going */
      switch (state->NextInstr) {
        case NONSEQ:
        case SEQ:
          SETPC(state->Reg[15]+4); /* Advance the pipeline, and an S cycle */
          pc += 4;

#if PIPESIZE == 3
          ARMul_LoadInstr(state, pc + 8, &pipe[pipeidx]);
          pipeidx=(pipeidx<2?pipeidx+1:0);
#else
          pipeidx=(pipeidx+1)&3;
          ARMul_LoadInstr(state, pc + 8, &pipe[pipeidx^2]);
#endif
          break;

        case PCINCEDSEQ:
        case PCINCEDNONSEQ:
          /* DAG: R15 already advanced? */
          pc += 4; /* Program counter advanced, and an S cycle */
#if PIPESIZE == 3
          ARMul_LoadInstr(state, pc + 8, &pipe[pipeidx]);
          pipeidx=(pipeidx<2?pipeidx+1:0);
#else
          pipeidx=(pipeidx+1)&3;
          ARMul_LoadInstr(state, pc + 8, &pipe[pipeidx^2]);
#endif
          NORMALCYCLE;
          break;

        /* DAG - resume was here! */

        default: /* The program counter has been changed */
#ifdef DEBUG
          printf("PC ch pc=0x%x (O 0x%x\n", state->Reg[15], pc);
#endif
          pc = PC;
          SETPC(pc+8);
          state->Aborted = 0;
          pipeidx=0;
          ARMul_LoadInstrTriplet(state, pc, pipe);
          NORMALCYCLE;
          break;
      }
      ARMul_InvokeEvent(state);

      if (ARMul_Time >= ioc.NextTimerTrigger)
        UpdateTimerRegisters(state);

      if (state->Exception) { /* Any exceptions */
        if ((state->Exception & 2) && !FFLAG) {
          ARMul_Abort(state, ARMul_FIQV);
          break;

        } else if ((state->Exception & 1) && !IFLAG) {
          ARMul_Abort(state, ARMul_IRQV);
          break;
        }
      }

      ARMword instr = pipe[pipeidx].instr;
      /*fprintf(stderr, "exec: pc=0x%08x instr=0x%08x\n", pc, instr);*/
      if(ARMul_CCCheck(instr,ECC))
        (pipe[pipeidx].func)(state, instr, pc);

    } /* for loop */

    state->decoded = pipe[(pipeidx+1)%PIPESIZE].instr;
    state->loaded = pipe[(pipeidx+2)%PIPESIZE].instr;
    state->pc = pc;
  }
} /* Emulate 26 in instruction based mode */
