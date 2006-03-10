/* DANGER: The instruction read may have changed the ROM mapping and thus it might
   not be the same when you write the instruction function back ! */
/* Also there might be an intervening write which overwrites one of the prefetched
   instructions and thus we mustnt overwrite it with a function corresponding to its
   old value! */

   /* MUST modify write code to invalidate pInstrFunc etc. */

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

ARMword *armflags = &statestr.NFlag;

static ARMEmuFunc* pInstrFunc;  /* These are only used by the writeback of the instruction function pointer */
static ARMEmuFunc* pDecFunc;    /* But they are invalidated by word/byte write to point to a dummy location */
static ARMEmuFunc* pLoadedFunc; /* So that a fetched instruction followed by an overwrite of its location   */
                        /* doesn't get its function pointer overwritten by the original instruction */

/***************************************************************************\
*                   Load Instruction                                        *
\***************************************************************************/

static ARMword
ARMul_LoadInstr(ARMword address, ARMEmuFunc **func)
{
  static ARMEmuFunc badfunc;
  ARMword PageTabVal;

  statestr.NumCycles++;
  address &= 0x3ffffff;

  /* First check if we are doing ROM remapping and are accessing a remapped */
  /* location.  If it is then map it into the High ROM space                */
  if ((MEMC.ROMMapFlag) && (address < 0x800000)) {
    MEMC.ROMMapFlag = 2;
    /* A simple ROM wrap around */
    address %= MEMC.ROMHighSize;

    ARMul_CLEARABORT;
    *func = &MEMC.ROMHighFuncs[address / 4];
    return MEMC.ROMHigh[address / 4];
  }

  switch ((address >> 24) & 3) {
    case 3:
      /* If we are in ROM and trying to read then there is no hassle */
      if (address >= MEMORY_0x3400000_R_ROM_LOW) {
        /* It's ROM read - no hassle */
        ARMul_CLEARABORT;

        if (MEMC.ROMMapFlag == 2) {
          MEMC.OldAddress1 = -1;
          MEMC.OldPage1 = -1;
          MEMC.ROMMapFlag = 0;
        }

        if (address >= MEMORY_0x3800000_R_ROM_HIGH) {
          address -= MEMORY_0x3800000_R_ROM_HIGH;
          address %= MEMC.ROMHighSize;

          *func = &MEMC.ROMHighFuncs[address / 4];
          return MEMC.ROMHigh[address / 4];
        } else {
          if (MEMC.ROMLow) {
            address -= MEMORY_0x3400000_R_ROM_LOW;
            address %= MEMC.ROMLowSize;

            *func = &MEMC.ROMLowFuncs[address / 4];
            return MEMC.ROMLow[address / 4];
          } else {
            return 0;
          }
        }
      } else {
        if (statestr.NtransSig) {

          if (MEMC.ROMMapFlag == 2) {
            MEMC.ROMMapFlag = 0;
            MEMC.OldAddress1 = -1;
            MEMC.OldPage1 = -1;
          }
          badfunc = ARMul_Emulate_DecodeInstr;
          *func = &badfunc;
          return GetWord_IO(emu_state, address);
        } else {
          ARMul_PREFETCHABORT(address);
          badfunc = ARMul_Emulate_DecodeInstr;
          *func = &badfunc;
          return ARMul_ABORTWORD;
        }
      }
      /* NOTE: No break, IOC has same permissions as physical RAM */

    case 2:
      /* Privileged only */
      if (statestr.NtransSig) {
        ARMul_CLEARABORT;
        if (MEMC.ROMMapFlag == 2) {
          MEMC.ROMMapFlag = 0;
          MEMC.OldAddress1 = -1;
          MEMC.OldPage1 = -1;
        }
        address -= MEMORY_0x2000000_RAM_PHYS;

        /* I used to think memory wrapped after the real RAM - but it doesn't appear to */
        if (address >= MEMC.RAMSize) {
          fprintf(stderr, "GetWord returning dead0bad - after PhysRam\n");
          badfunc = ARMul_Emulate_DecodeInstr;
          *func = &badfunc;
          return 0xdead0bad;
          /*while (address > MEMC.RAMSize)
            address -= MEMC.RAMSize; */
        }

        *func = &MEMC.PhysRamfuncs[address / 4];
        return MEMC.PhysRam[address / 4];
      } else {
        ARMul_PREFETCHABORT(address);
        badfunc = ARMul_Emulate_DecodeInstr;
        *func = &badfunc;
        return ARMul_ABORTWORD;
      }
      break;

    case 0:
    case 1:
#ifdef DEBUG
      printf("ARMul_LoadInstr (logical map) for address=0x%x\n", address);
#endif
      /* OK - it's in logically mapped RAM */
      if (!FindPageTableEntry(address, &PageTabVal)) {
        badfunc = ARMul_Emulate_DecodeInstr;
        *func = &badfunc;
        ARMul_PREFETCHABORT(address);
        return ARMul_ABORTWORD;
      }

      MEMC.TmpPage = PageTabVal;

      /* If it's not supervisor, and not OS mode, and the page is read protected then prefetch abort */
      if ((!(statestr.NtransSig)) && (!(MEMC.ControlReg & (1 << 12))) &&
          (PageTabVal & 512))
      {
        ARMul_PREFETCHABORT(address);
        badfunc = ARMul_Emulate_DecodeInstr;
        *func = &badfunc;
        return ARMul_ABORTWORD;
      }
  }

  ARMul_CLEARABORT;
  /* Logically mapped RAM */
  address = GetPhysAddress(address);

  /* I used to think memory wrapped after the real RAM - but it doesn't appear
     to */
  if (address >= MEMC.RAMSize) {
    fprintf(stderr, "GetWord returning dead0bad - after PhysRam (from log ram)\n");
    badfunc = ARMul_Emulate_DecodeInstr;
    *func = &badfunc;
    return 0xdead0bad;
    /*while (address > MEMC.RAMSize)
      address -= MEMC.RAMSize; */
  }

  *func = &MEMC.PhysRamfuncs[address / 4];
  return MEMC.PhysRam[address / 4];
}

/* --------------------------------------------------------------------------- */
static void
ARMul_LoadInstrTriplet(ARMword address,
                       ARMword *pw1, ARMword *pw2, ARMword *pw3,
                       ARMEmuFunc **func1, ARMEmuFunc **func2,
                       ARMEmuFunc **func3)
{
  static ARMEmuFunc badfunc;
  ARMword PageTabVal;

  if ((MEMC.ROMMapFlag) || ((address & 0xfff) > 0xff0)) {
    *pw1 = ARMul_LoadInstr(address, func1);
    *pw2 = ARMul_LoadInstr(address + 4, func2);
    *pw3 = ARMul_LoadInstr(address + 8, func3);
    return;
  }

  statestr.NumCycles += 3;
  address &= 0x3ffffff;

  switch ((address >> 24) & 3) {
    case 3:
      /* If we are in ROM and trying to read then there is no hassle */
      if (address >= MEMORY_0x3400000_R_ROM_LOW) {
        /* It's ROM read - no hassle */
        ARMul_CLEARABORT;

        if (MEMC.ROMMapFlag == 2) {
          MEMC.OldAddress1 = -1;
          MEMC.OldPage1 = -1;
          MEMC.ROMMapFlag = 0;
        }

        if (address >= MEMORY_0x3800000_R_ROM_HIGH) {
          address -= MEMORY_0x3800000_R_ROM_HIGH;
          address %= MEMC.ROMHighSize;
          address /= 4;
          *pw1 = MEMC.ROMHigh[address];
          *pw2 = MEMC.ROMHigh[address + 1];
          *pw3 = MEMC.ROMHigh[address + 2];
          *func1 = &MEMC.ROMHighFuncs[address];
          *func2 = &MEMC.ROMHighFuncs[address + 1];
          *func3 = &MEMC.ROMHighFuncs[address + 2];
        } else {
          if (MEMC.ROMLow) {
            address -= MEMORY_0x3400000_R_ROM_LOW;
            address %= MEMC.ROMLowSize;
            address /= 4;
            *pw1 = MEMC.ROMLow[address];
            *pw2 = MEMC.ROMLow[address + 1];
            *pw3 = MEMC.ROMLow[address + 2];
            *func1 = &MEMC.ROMLowFuncs[address];
            *func2 = &MEMC.ROMLowFuncs[address + 1];
            *func3 = &MEMC.ROMLowFuncs[address + 2];
          } else {
            /* We should never reach here:
             * Because no ROM is mapped in the ROM Low address space,
             * reading data from it will return 0,
             * and RISC OS should not attempt to execute code from */
            abort();
          }
        }
        return;
      } else {
        if (statestr.NtransSig) {

          if (MEMC.ROMMapFlag == 2) {
            MEMC.ROMMapFlag = 0;
            MEMC.OldAddress1 = -1;
            MEMC.OldPage1 = -1;
          }
          *pw1 = GetWord_IO(emu_state, address);
          *pw2 = GetWord_IO(emu_state, address + 4);
          *pw3 = GetWord_IO(emu_state, address + 8);
        } else {
          ARMul_PREFETCHABORT(address);
          ARMul_PREFETCHABORT(address + 4);
          ARMul_PREFETCHABORT(address + 8);
          *pw1 = ARMul_ABORTWORD;
          *pw2 = ARMul_ABORTWORD;
          *pw3 = ARMul_ABORTWORD;
        }
        badfunc = ARMul_Emulate_DecodeInstr;
        *func1 = &badfunc;
        *func2 = &badfunc;
        *func3 = &badfunc;
        return;
      }
      /* NOTE: No break, IOC has same permissions as physical RAM */

    case 2:
      /* Privileged only */
      if (statestr.NtransSig) {
        ARMul_CLEARABORT;
        if (MEMC.ROMMapFlag == 2) {
          MEMC.ROMMapFlag = 0;
          MEMC.OldAddress1 = -1;
          MEMC.OldPage1 = -1;
        }
        address -= MEMORY_0x2000000_RAM_PHYS;

        /* I used to think memory wrapped after the real RAM - but it doesn't appear to */
        if (address >= MEMC.RAMSize) {
          fprintf(stderr, "LoadInstrTriplet returning dead0bad - after PhysRam address=%x+0x2000000\n", address);
          *pw1 = 0xdead0bad;
          *pw2 = 0xdead0bad;
          *pw3 = 0xdead0bad;
          badfunc = ARMul_Emulate_DecodeInstr;
          *func1 = &badfunc;
          *func2 = &badfunc;
          *func3 = &badfunc;
          return;
          /*while (address>MEMC.RAMSize)
            address-=MEMC.RAMSize; */
        }

        address /= 4;
        *pw1 = MEMC.PhysRam[address];
        *pw2 = MEMC.PhysRam[address + 1];
        *pw3 = MEMC.PhysRam[address + 2];
        *func1 = &MEMC.PhysRamfuncs[address];
        *func2 = &MEMC.PhysRamfuncs[address + 1];
        *func3 = &MEMC.PhysRamfuncs[address + 2];
      } else {
        ARMul_PREFETCHABORT(address);
        ARMul_PREFETCHABORT(address + 4);
        ARMul_PREFETCHABORT(address + 8);
        *pw1 = ARMul_ABORTWORD;
        *pw2 = ARMul_ABORTWORD;
        *pw3 = ARMul_ABORTWORD;
        badfunc = ARMul_Emulate_DecodeInstr;
        *func1 = &badfunc;
        *func2 = &badfunc;
        *func3 = &badfunc;
      }
      return;
      break;

    case 0:
    case 1:
#ifdef DEBUG
      printf("LITrip (lmap) add=0x%x\n", address);
#endif
      /* OK - it's in logically mapped RAM */
      if (!FindPageTableEntry(address, &PageTabVal)) {
        ARMul_PREFETCHABORT(address);
        ARMul_PREFETCHABORT(address + 4);
        ARMul_PREFETCHABORT(address + 8);
        *pw1 = ARMul_ABORTWORD;
        *pw2 = ARMul_ABORTWORD;
        *pw3 = ARMul_ABORTWORD;
        badfunc = ARMul_Emulate_DecodeInstr;
        *func1 = &badfunc;
        *func2 = &badfunc;
        *func3 = &badfunc;
        return;
      }

      MEMC.TmpPage = PageTabVal;

#ifdef DEBUG
      printf("LITrip(lmap) add=0x%x PTab=0x%x NT=%d MEMC.Ctrl b 12=%d\n",
             address, PageTabVal, statestr.NtransSig,
             MEMC.ControlReg & (1 << 12));
#endif

      /* The page exists - so if itis supervisor then it's OK */
      if ((!(statestr.NtransSig)) && (!(MEMC.ControlReg & (1 << 12))) &&
          (PageTabVal & 512))
      {
        ARMul_PREFETCHABORT(address);
        ARMul_PREFETCHABORT(address + 4);
        ARMul_PREFETCHABORT(address + 8);
        *pw1 = ARMul_ABORTWORD;
        *pw2 = ARMul_ABORTWORD;
        *pw3 = ARMul_ABORTWORD;
        badfunc = ARMul_Emulate_DecodeInstr;
        *func1 = &badfunc;
        *func2 = &badfunc;
        *func3 = &badfunc;
#ifdef DEBUG
        printf("LITrip(lmap) prefetchabort - no access to usermode address=0x%x\n",
               address);
#endif
        return;
      }
  }
  ARMul_CLEARABORT;

  /* Logically mapped RAM */
  address = GetPhysAddress(address);

  /* I used to think memory wrapped after the real RAM - but it doesn't appear
     to */
  if (address >= MEMC.RAMSize) {
    fprintf(stderr, "LoadInstrTriplet returning dead0bad - after PhysRam (from log ram)\n");
    *pw1 = 0xdead0bad;
    *pw2 = 0xdead0bad;
    *pw3 = 0xdead0bad;
    badfunc = ARMul_Emulate_DecodeInstr;
    *func1 = &badfunc;
    *func2 = &badfunc;
    *func3 = &badfunc;
    return;
    /*while (address > MEMC.RAMSize)
      address -= MEMC.RAMSize; */
  }

  address /= 4;
  *pw1 = MEMC.PhysRam[address];
  *pw2 = MEMC.PhysRam[address + 1];
  *pw3 = MEMC.PhysRam[address + 2];
  *func1 = &MEMC.PhysRamfuncs[address];
  *func2 = &MEMC.PhysRamfuncs[address + 1];
  *func3 = &MEMC.PhysRamfuncs[address + 2];
}

/***************************************************************************\
* This routine is called at the beginning of every cycle, to invoke         *
* scheduled events.                                                         *
\***************************************************************************/

static void ARMul_InvokeEvent(void)
{
  if (statestr.Now < ARMul_Time) {
    DisplayKbd_Poll(&statestr);
  }
}


void ARMul_Icycles(unsigned number)
{
  statestr.NumCycles += number;
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

#ifndef __riscos__
static ARMword
GetDPRegRHS(ARMul_State *state, ARMword instr)
{
  ARMword shamt , base;

#ifndef MODE32
 base = RHSReg;
#endif
 if (BIT(4)) { /* shift amount in a register */
    UNDEF_Shift;
    INCPC;
#ifndef MODE32
    if (base == 15)
       base = ECC | ER15INT | R15PC | EMODE;
    else
#endif
       base = statestr.Reg[base];
    ARMul_Icycles(1);
    shamt = statestr.Reg[BITS(8,11)] & 0xff;
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
#ifndef MODE32
    if (base == 15)
      base = ECC | ER15INT | R15PC | EMODE;
    else
#endif
       base = statestr.Reg[base];
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

/***************************************************************************\
* This routine evaluates most Logical Data Processing register RHSs        *
* with the S bit set.  It is intended to be called from the macro           *
* DPSRegRHS, which filters the common case of an unshifted register         *
* with in line code                                                         *
\***************************************************************************/

#ifndef __riscos__
static ARMword
GetDPSRegRHS(ARMul_State *state, ARMword instr)
{
  ARMword shamt , base;

#ifndef MODE32
 base = RHSReg;
#endif
 if (BIT(4)) { /* shift amount in a register */
    UNDEF_Shift;
    INCPC;
#ifndef MODE32
    if (base == 15)
       base = ECC | ER15INT | R15PC | EMODE;
    else
#endif
       base = statestr.Reg[base];
    ARMul_Icycles(1);
    shamt = statestr.Reg[BITS(8,11)] & 0xff;
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
#ifndef MODE32
    if (base == 15)
       base = ECC | ER15INT | R15PC | EMODE;
    else
#endif
       base = statestr.Reg[base];
    shamt = BITS(7,11);
    switch ((int)BITS(5,6)) {
       case LSL: ASSIGNC((base >> (32-shamt)) & 1);
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
#ifdef MODE32
 statestr.Reg[15] = src & PCBITS;
#else
 statestr.Reg[15] = (src & R15PCBITS) | ECC | ER15INT | EMODE;
 ARMul_R15Altered(state);
#endif
 FLUSHPIPE;
 }


/***************************************************************************\
* This routine handles writes to register 15 when the S bit is set.         *
\***************************************************************************/

static void WriteSR15(ARMul_State *state, ARMword src)
{
#ifdef MODE32
 statestr.Reg[15] = src & PCBITS;
 if (statestr.Bank > 0) {
    statestr.Cpsr = statestr.Spsr[statestr.Bank];
    ARMul_CPSRAltered(state);
    }
#else
 if (statestr.Bank == USERBANK)
    statestr.Reg[15] = (src & (CCBITS | R15PCBITS)) | ER15INT | EMODE;
 else
    statestr.Reg[15] = src;
 ARMul_R15Altered(state);
#endif
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
#ifndef MODE32
 if (base == 15)
    base = ECC | ER15INT | R15PC | EMODE; /* Now forbidden, but .... */
 else
#endif
    base = statestr.Reg[base];

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
#ifndef MODE32
 if (ADDREXCEPT(address)) {
    INTERNALABORT(address);
    }
#endif
 dest = ARMul_LoadWordN(&statestr,address);
 if (statestr.Aborted) {
    TAKEABORT;
    return(statestr.lateabtSig);
    }
 if (address & 3)
    dest = ARMul_Align(&statestr,address,dest);
 WRITEDEST(dest);
 ARMul_Icycles(1);

 return(DESTReg != LHSReg);
}

/***************************************************************************\
* This function does the work of loading a byte for a LDRB instruction.     *
\***************************************************************************/

static unsigned LoadByte(ARMul_State *state, ARMword instr, ARMword address)
{
 ARMword dest;

 BUSUSEDINCPCS;
#ifndef MODE32
 if (ADDREXCEPT(address)) {
    INTERNALABORT(address);
    }
#endif
 dest = ARMul_LoadByte(&statestr,address);
 if (statestr.Aborted) {
    TAKEABORT;
    return(statestr.lateabtSig);
    }
 UNDEF_LSRBPC;
 WRITEDEST(dest);
 ARMul_Icycles(1);
 return(DESTReg != LHSReg);
}

/***************************************************************************\
* This function does the work of storing a word from a STR instruction.     *
\***************************************************************************/

static unsigned StoreWord(ARMul_State *state, ARMword instr, ARMword address)
{BUSUSEDINCPCN;
#ifndef MODE32
 if (DESTReg == 15)
    statestr.Reg[15] = ECC | ER15INT | R15PC | EMODE;
#endif
#ifdef MODE32
 ARMul_StoreWordN(&statestr,address,DEST);
#else
 if (ADDREXCEPT(address)) {
    INTERNALABORT(address);
    (void)ARMul_LoadWordN(&statestr,address);
    }
 else
    ARMul_StoreWordN(&statestr,address,DEST);
#endif
 if (statestr.Aborted) {
    TAKEABORT;
    return(statestr.lateabtSig);
    }
 return(TRUE);
}

/***************************************************************************\
* This function does the work of storing a byte for a STRB instruction.     *
\***************************************************************************/

static unsigned StoreByte(ARMul_State *state, ARMword instr, ARMword address)
{BUSUSEDINCPCN;
#ifndef MODE32
 if (DESTReg == 15)
    statestr.Reg[15] = ECC | ER15INT | R15PC | EMODE;
#endif
#ifdef MODE32
 ARMul_StoreByte(&statestr,address,DEST);
#else
 if (ADDREXCEPT(address)) {
    INTERNALABORT(address);
    (void)ARMul_LoadByte(&statestr,address);
    }
 else
    ARMul_StoreByte(&statestr,address,DEST);
#endif
 if (statestr.Aborted) {
    TAKEABORT;
    return(statestr.lateabtSig);
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
{ARMword dest, temp;

 UNDEF_LSMNoRegs;
 UNDEF_LSMPCBase;
 UNDEF_LSMBaseInListWb;
 BUSUSEDINCPCS;
#ifndef MODE32
 if (ADDREXCEPT(address)) {
    INTERNALABORT(address);
    }
#endif
 if (BIT(21) && LHSReg != 15)
    LSBase = WBBase;

    for (temp = 0; !BIT(temp); temp++); /* N cycle first */
    dest = ARMul_LoadWordN(&statestr,address);
    if (!statestr.abortSig && !statestr.Aborted)
       statestr.Reg[temp] = dest;
    else
       if (!statestr.Aborted)
          statestr.Aborted = ARMul_DataAbortV;

    temp++;

    for (; temp < 16; temp++) /* S cycles from here on */
       if (BIT(temp)) { /* load this register */
          address += 4;
          dest = ARMul_LoadWordS(&statestr,address);
          if (!statestr.abortSig && !statestr.Aborted)
             statestr.Reg[temp] = dest;
          else
             if (!statestr.Aborted)
                statestr.Aborted = ARMul_DataAbortV;
          }

 if ((BIT(15)) && (!statestr.abortSig && !statestr.Aborted)) {
   /* PC is in the reg list */
#ifdef MODE32
    statestr.Reg[15] = PC;
#endif
    FLUSHPIPE;
    }

 ARMul_Icycles(1); /* to write back the final register */

 if (statestr.Aborted) {
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
{ARMword dest, temp;

 UNDEF_LSMNoRegs;
 UNDEF_LSMPCBase;
 UNDEF_LSMBaseInListWb;
 BUSUSEDINCPCS;
#ifndef MODE32
 if (ADDREXCEPT(address)) {
    INTERNALABORT(address);
    }
#endif

 /* Actually do the write back (Hey guys - this is in after the mode change!) DAG */
 if (BIT(21) && LHSReg != 15)
    LSBase = WBBase;

 if (!BIT(15) && statestr.Bank != USERBANK) {
    (void)ARMul_SwitchMode(&statestr,statestr.Mode,USER26MODE); /* temporary reg bank switch */
    UNDEF_LSMUserBankWb;
    }

    for (temp = 0; !BIT(temp); temp++); /* N cycle first */
    dest = ARMul_LoadWordN(&statestr,address);
    if (!statestr.abortSig)
       statestr.Reg[temp] = dest;
    else
       if (!statestr.Aborted)
          statestr.Aborted = ARMul_DataAbortV;
    temp++;

    for (; temp < 16; temp++) /* S cycles from here on */
       if (BIT(temp)) { /* load this register */
          address += 4;
          dest = ARMul_LoadWordS(&statestr,address);
          if (!(statestr.abortSig || statestr.Aborted))
             statestr.Reg[temp] = dest;
          else
             if (!statestr.Aborted)
                statestr.Aborted = ARMul_DataAbortV;
          }

 /* DAG - stop corruption of R15 after an abort */
 if (!(statestr.abortSig || statestr.Aborted)) {
   if (BIT(15)) { /* PC is in the reg list */
#ifdef MODE32
      if (statestr.Mode != USER26MODE && statestr.Mode != USER32MODE) {
         statestr.Cpsr = GETSPSR(statestr.Bank);
         ARMul_CPSRAltered(state);
         }
      statestr.Reg[15] = PC;
#else
      if (statestr.Mode == USER26MODE) { /* protect bits in user mode */
         ASSIGNN((statestr.Reg[15] & NBIT) != 0);
         ASSIGNZ((statestr.Reg[15] & ZBIT) != 0);
         ASSIGNC((statestr.Reg[15] & CBIT) != 0);
         ASSIGNV((statestr.Reg[15] & VBIT) != 0);
         }
      else
         ARMul_R15Altered(state);
#endif
      FLUSHPIPE;
      }

 }
 if (!BIT(15) && statestr.Mode != USER26MODE)
    (void)ARMul_SwitchMode(&statestr,USER26MODE,statestr.Mode); /* restore the correct bank */
 ARMul_Icycles(1); /* to write back the final register */

 if (statestr.Aborted) {
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
#ifndef MODE32
 if (ADDREXCEPT(address)) {
    INTERNALABORT(address);
    }
 if (BIT(15))
    PATCHR15;
#endif

 for (temp = 0; !BIT(temp); temp++); /* N cycle first */
#ifdef MODE32
 ARMul_StoreWordN(&statestr,address,statestr.Reg[temp++]);
#else
 if (statestr.Aborted) {
    (void)ARMul_LoadWordN(&statestr,address);
    for (; temp < 16; temp++) /* Fake the Stores as Loads */
       if (BIT(temp)) { /* save this register */
          address += 4;
          (void)ARMul_LoadWordS(&statestr,address);
          }
    if (BIT(21) && LHSReg != 15)
       LSBase = WBBase;
    TAKEABORT;
    return;
    }
 else
    ARMul_StoreWordN(&statestr,address,statestr.Reg[temp++]);
#endif
 if (statestr.abortSig && !statestr.Aborted)
    statestr.Aborted = ARMul_DataAbortV;

 if (BIT(21) && LHSReg != 15)
    LSBase = WBBase;

 for (; temp < 16; temp++) /* S cycles from here on */
    if (BIT(temp)) { /* save this register */
       address += 4;
       ARMul_StoreWordS(&statestr,address,statestr.Reg[temp]);
       if (statestr.abortSig && !statestr.Aborted)
             statestr.Aborted = ARMul_DataAbortV;
       }
    if (statestr.Aborted) {
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
#ifndef MODE32
 if (ADDREXCEPT(address)) {
    INTERNALABORT(address);
    }
 if (BIT(15))
    PATCHR15;
#endif

 if (statestr.Bank != USERBANK) {
    (void)ARMul_SwitchMode(&statestr,statestr.Mode,USER26MODE); /* Force User Bank */
    UNDEF_LSMUserBankWb;
    }

 for (temp = 0; !BIT(temp); temp++); /* N cycle first */
#ifdef MODE32
 ARMul_StoreWordN(&statestr,address,statestr.Reg[temp++]);
#else
 if (statestr.Aborted) {
    (void)ARMul_LoadWordN(&statestr,address);
    for (; temp < 16; temp++) /* Fake the Stores as Loads */
       if (BIT(temp)) { /* save this register */
          address += 4;
          (void)ARMul_LoadWordS(&statestr,address);
          }
    if (BIT(21) && LHSReg != 15)
       LSBase = WBBase;
    TAKEABORT;
    return;
    }
 else
    ARMul_StoreWordN(&statestr,address,statestr.Reg[temp++]);
#endif
 if (statestr.abortSig && !statestr.Aborted)
    statestr.Aborted = ARMul_DataAbortV;

 if (BIT(21) && LHSReg != 15)
    LSBase = WBBase;

 for (; temp < 16; temp++) /* S cycles from here on */
    if (BIT(temp)) { /* save this register */
       address += 4;
       ARMul_StoreWordS(&statestr,address,statestr.Reg[temp]);
       if (statestr.abortSig && !statestr.Aborted)
             statestr.Aborted = ARMul_DataAbortV;
       }

 if (statestr.Mode != USER26MODE)
    (void)ARMul_SwitchMode(&statestr,USER26MODE,statestr.Mode); /* restore the correct bank */

 if (statestr.Aborted) {
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
#define EMFUNCDECL26(name) ARMul_Emulate26_ ## name ## _EQ
#define EMFUNC_CONDTEST if (!ZFLAG) return;
#include "armemuinstr.c"

#undef EMFUNCDECL26
#undef EMFUNC_CONDTEST
#define EMFUNCDECL26(name) ARMul_Emulate26_ ## name ## _NE
#define EMFUNC_CONDTEST if (ZFLAG) return;

#include "armemuinstr.c"

#undef EMFUNCDECL26
#undef EMFUNC_CONDTEST
#define EMFUNCDECL26(name) ARMul_Emulate26_ ## name ## _VS
#define EMFUNC_CONDTEST if (!VFLAG) return;
#include "armemuinstr.c"

#undef EMFUNCDECL26
#undef EMFUNC_CONDTEST
#define EMFUNCDECL26(name) ARMul_Emulate26_ ## name ## _VC
#define EMFUNC_CONDTEST if (VFLAG) return;

#include "armemuinstr.c"

#undef EMFUNCDECL26
#undef EMFUNC_CONDTEST
#define EMFUNCDECL26(name) ARMul_Emulate26_ ## name ## _MI
#define EMFUNC_CONDTEST if (!NFLAG) return;
#include "armemuinstr.c"

#undef EMFUNCDECL26
#undef EMFUNC_CONDTEST
#define EMFUNCDECL26(name) ARMul_Emulate26_ ## name ## _PL
#define EMFUNC_CONDTEST if (NFLAG) return;

#include "armemuinstr.c"

#undef EMFUNCDECL26
#undef EMFUNC_CONDTEST
#define EMFUNCDECL26(name) ARMul_Emulate26_ ## name ## _CS
#define EMFUNC_CONDTEST if (!CFLAG) return;
#include "armemuinstr.c"

#undef EMFUNCDECL26
#undef EMFUNC_CONDTEST
#define EMFUNCDECL26(name) ARMul_Emulate26_ ## name ## _CC
#define EMFUNC_CONDTEST if (CFLAG) return;

#include "armemuinstr.c"

#undef EMFUNCDECL26
#undef EMFUNC_CONDTEST
#define EMFUNCDECL26(name) ARMul_Emulate26_ ## name ## _HI
#define EMFUNC_CONDTEST if (!(CFLAG && !ZFLAG)) return;

#include "armemuinstr.c"

#undef EMFUNCDECL26
#undef EMFUNC_CONDTEST
#define EMFUNCDECL26(name) ARMul_Emulate26_ ## name ## _LS
#define EMFUNC_CONDTEST if (!((!CFLAG || ZFLAG))) return;

#include "armemuinstr.c"

#undef EMFUNCDECL26
#undef EMFUNC_CONDTEST
#define EMFUNCDECL26(name) ARMul_Emulate26_ ## name ## _GE
#define EMFUNC_CONDTEST if (!((!NFLAG && !VFLAG) || (NFLAG && VFLAG))) return;

#include "armemuinstr.c"

#undef EMFUNCDECL26
#undef EMFUNC_CONDTEST
#define EMFUNCDECL26(name) ARMul_Emulate26_ ## name ## _LT
#define EMFUNC_CONDTEST if (!((NFLAG && !VFLAG) || (!NFLAG && VFLAG))) return;

#include "armemuinstr.c"

#undef EMFUNCDECL26
#undef EMFUNC_CONDTEST
#define EMFUNCDECL26(name) ARMul_Emulate26_ ## name ## _GT
#define EMFUNC_CONDTEST if (!((!NFLAG && !VFLAG && !ZFLAG) || (NFLAG && VFLAG && !ZFLAG))) return;

#include "armemuinstr.c"

#undef EMFUNCDECL26
#undef EMFUNC_CONDTEST
#define EMFUNCDECL26(name) ARMul_Emulate26_ ## name ## _LE
#define EMFUNC_CONDTEST if (!(((NFLAG && !VFLAG) || (!NFLAG && VFLAG)) || ZFLAG)) return;

#include "armemuinstr.c"

/* ################################################################################## */
/* ## Function called when the decode is unknown                                   ## */
/* ################################################################################## */
void ARMul_Emulate_DecodeInstr(ARMword instr, ARMword pc) {
  ARMEmuFunc f;
  switch ((int)TOPBITS(28)) {
    case AL:

#undef EMFUNCDECL26
#define EMFUNCDECL26(name) ARMul_Emulate26_ ## name
#include "armemudec.c"
      break;

    case EQ:
#undef EMFUNCDECL26
#define EMFUNCDECL26(name) ARMul_Emulate26_ ## name ## _EQ
#include "armemudec.c"

      break;

    case NE:
#undef EMFUNCDECL26
#define EMFUNCDECL26(name) ARMul_Emulate26_ ## name ## _NE
#include "armemudec.c"
      break;

    case CS:
#undef EMFUNCDECL26
#define EMFUNCDECL26(name) ARMul_Emulate26_ ## name ## _CS
#include "armemudec.c"
      break;

    case CC:
#undef EMFUNCDECL26
#define EMFUNCDECL26(name) ARMul_Emulate26_ ## name ## _CC
#include "armemudec.c"
      break;

    case VS:
#undef EMFUNCDECL26
#define EMFUNCDECL26(name) ARMul_Emulate26_ ## name ## _VS
#include "armemudec.c"
      break;

    case VC:
#undef EMFUNCDECL26
#define EMFUNCDECL26(name) ARMul_Emulate26_ ## name ## _VC
#include "armemudec.c"
      break;

    case MI:
#undef EMFUNCDECL26
#define EMFUNCDECL26(name) ARMul_Emulate26_ ## name ## _MI
#include "armemudec.c"
      break;

    case PL:
#undef EMFUNCDECL26
#define EMFUNCDECL26(name) ARMul_Emulate26_ ## name ## _PL
#include "armemudec.c"
      break;

    case HI:
#undef EMFUNCDECL26
#define EMFUNCDECL26(name) ARMul_Emulate26_ ## name ## _HI
#include "armemudec.c"
      break;

    case LS:
#undef EMFUNCDECL26
#define EMFUNCDECL26(name) ARMul_Emulate26_ ## name ## _LS
#include "armemudec.c"
      break;

    case GE:
#undef EMFUNCDECL26
#define EMFUNCDECL26(name) ARMul_Emulate26_ ## name ## _GE
#include "armemudec.c"
      break;

    case LT:
#undef EMFUNCDECL26
#define EMFUNCDECL26(name) ARMul_Emulate26_ ## name ## _LT
#include "armemudec.c"
      break;

    case GT:
#undef EMFUNCDECL26
#define EMFUNCDECL26(name) ARMul_Emulate26_ ## name ## _GT
#include "armemudec.c"
      break;

    case LE:
#undef EMFUNCDECL26
#define EMFUNCDECL26(name) ARMul_Emulate26_ ## name ## _LE
#include "armemudec.c"
      break;

    default:
      f = ARMul_Emulate26_Noop;
      break;
  };

  /* Update the instruction emulation pointer */
  *pInstrFunc=f;

  f(instr,pc);
} /* ARMul_Emulate_DecodeInstr */

void
ARMul_Emulate26(void)
{
  ARMword instr,           /* The current instruction */
          pc = 0;          /* The address of the current instruction */
  ARMword decoded=0, loaded=0; /* Instruction pipeline */
  /* Function of decoded instruction functions */
  ARMEmuFunc instrfunc, decfunc = NULL, loadedfunc = NULL;
  static ARMEmuFunc dummyfunc = ARMul_Emulate_DecodeInstr;

  /**************************************************************************\
   *                        Execute the next instruction                    *
  \**************************************************************************/
  kill_emulator = 0;
  while (kill_emulator == 0) {
    if (statestr.NextInstr < PRIMEPIPE) {
      decoded = statestr.decoded;
      loaded  = statestr.loaded;
      pc      = statestr.pc;

      decfunc = loadedfunc =
                instrfunc =
                ARMul_Emulate_DecodeInstr; /* Could do better here! */

      pInstrFunc = pDecFunc
                 = pLoadedFunc
                 = &dummyfunc;
    }

    for (;;) { /* just keep going */
      switch (statestr.NextInstr) {
        case NONSEQ:
        case SEQ:
          statestr.Reg[15] += 4; /* Advance the pipeline, and an S cycle */
          pc += 4;
          instr     = decoded;
          instrfunc = decfunc;
          pInstrFunc = pDecFunc;

          decoded  = loaded;
          decfunc  = loadedfunc;
          pDecFunc = pLoadedFunc;

          loaded = ARMul_LoadInstr(pc + 8, &pLoadedFunc);
          loadedfunc = *pLoadedFunc;
          break;

        case PCINCEDSEQ:
        case PCINCEDNONSEQ:
          /* DAG: R15 already advanced? */
          pc += 4; /* Program counter advanced, and an S cycle */
          instr      = decoded;
          instrfunc  = decfunc;
          pInstrFunc = pDecFunc;

          decoded  = loaded;
          decfunc  = loadedfunc;
          pDecFunc = pLoadedFunc;

          loaded = ARMul_LoadInstr(pc + 8, &pLoadedFunc);
          loadedfunc = *pLoadedFunc;
          NORMALCYCLE;
          break;

        /* DAG - resume was here! */

        default: /* The program counter has been changed */
#ifdef DEBUG
          printf("PC ch pc=0x%x (O 0x%x\n", statestr.Reg[15], pc);
#endif
          pc = statestr.Reg[15];
#ifndef MODE32
          pc &= R15PCBITS;
#endif
          statestr.Reg[15] = pc + 8;
          statestr.Aborted = 0;
          ARMul_LoadInstrTriplet(pc, &instr, &decoded, &loaded,
                                 &pInstrFunc, &pDecFunc, &pLoadedFunc);
          instrfunc  = *pInstrFunc;
          decfunc    = *pDecFunc;
          loadedfunc = *pLoadedFunc;
          NORMALCYCLE;
          break;
      }
      ARMul_InvokeEvent();

      if (ARMul_Time >= ioc.NextTimerTrigger)
        UpdateTimerRegisters();

      if (statestr.Exception) { /* Any exceptions */
        if ((statestr.Exception & 2) && !FFLAG) {
          ARMul_Abort(&statestr, ARMul_FIQV);
          break;

        } else if ((statestr.Exception & 1) && !IFLAG) {
          ARMul_Abort(&statestr, ARMul_IRQV);
          break;
        }
      }

      /*fprintf(stderr, "exec: pc=0x%08x instr=0x%08x\n", pc, instr);*/
      instrfunc(instr, pc);
    } /* for loop */

    statestr.decoded = decoded;
    statestr.loaded = loaded;
    statestr.pc = pc;
  }
} /* Emulate 26 in instruction based mode */
