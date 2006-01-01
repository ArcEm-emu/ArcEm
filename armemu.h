/*  armemu.h -- ARMulator emulation macros:  ARM6 Instruction Emulator.
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

/***************************************************************************\
*                           Condition code values                           *
\***************************************************************************/

#define EQ 0
#define NE 1
#define CS 2
#define CC 3
#define MI 4
#define PL 5
#define VS 6
#define VC 7
#define HI 8
#define LS 9
#define GE 10
#define LT 11
#define GT 12
#define LE 13
#define AL 14
#define NV 15

/***************************************************************************\
*                               Shift Opcodes                               *
\***************************************************************************/

#define LSL 0
#define LSR 1
#define ASR 2
#define ROR 3

/***************************************************************************\
*               Macros to twiddle the status flags and mode                 *
\***************************************************************************/

#define NBIT ((unsigned)1L << 31)
#define ZBIT (1L << 30)
#define CBIT (1L << 29)
#define VBIT (1L << 28)
#define IBIT (1L << 7)
#define FBIT (1L << 6)
#define IFBITS (3L << 6)
#define R15IBIT (1L << 27)
#define R15FBIT (1L << 26)
#define R15IFBITS (3L << 26)

#define POS(i) ( (~(i)) >> 31 )
#define NEG(i) ( (i) >> 31 )

#define NFLAG armflags[0]
#define SETN NFLAG = 1
#define CLEARN NFLAG = 0
#define ASSIGNN(res) NFLAG = res

#define ZFLAG armflags[1]
#define SETZ ZFLAG = 1
#define CLEARZ ZFLAG = 0
#define ASSIGNZ(res) ZFLAG = res

#define CFLAG armflags[2]
#define SETC CFLAG = 1
#define CLEARC CFLAG = 0
#define ASSIGNC(res) CFLAG = res

#define VFLAG armflags[3]
#define SETV VFLAG = 1
#define CLEARV VFLAG = 0
#define ASSIGNV(res) VFLAG = res

#define IFLAG (armflags[4] >> 1)
#define FFLAG (armflags[4] & 1)
#define IFFLAGS armflags[4]
#define ASSIGNINT(res) IFFLAGS = (((res) >> 6) & 3)
#define ASSIGNR15INT(res) IFFLAGS = (((res) >> 26) & 3)


#define CCBITS (0xf0000000L)
#define INTBITS (0xc0L)
#define PCBITS (0xfffffffcL)
#define MODEBITS (0x1fL)
#define R15INTBITS (3L << 26)
#define R15PCBITS (0x03fffffcL)
#define R15PCMODEBITS (0x03ffffffL)
#define R15MODEBITS (0x3L)

#ifdef MODE32
#define PCMASK PCBITS
#define PCWRAP(pc) (pc)
#else
#define PCMASK R15PCBITS
#define PCWRAP(pc) ((pc) & R15PCBITS)
#endif
#define PC (statestr.Reg[15] & PCMASK)
#define R15CCINTMODE (statestr.Reg[15] & (CCBITS | R15INTBITS | R15MODEBITS))
#define R15INT (statestr.Reg[15] & R15INTBITS)
#define R15INTPC (statestr.Reg[15] & (R15INTBITS | R15PCBITS))
#define R15INTPCMODE (statestr.Reg[15] & (R15INTBITS | R15PCBITS | R15MODEBITS))
#define R15INTMODE (statestr.Reg[15] & (R15INTBITS | R15MODEBITS))
#define R15PC (statestr.Reg[15] & R15PCBITS)
#define R15PCMODE (statestr.Reg[15] & (R15PCBITS | R15MODEBITS))
#define R15MODE (statestr.Reg[15] & R15MODEBITS)

#define ECC ((NFLAG << 31) | (ZFLAG << 30) | (CFLAG << 29) | (VFLAG << 28))
#define EINT (IFFLAGS << 6)
#define ER15INT (IFFLAGS << 26)
#define EMODE (statestr.Mode)

#define CPSR (ECC | EINT | EMODE)
#ifdef MODE32
#define PATCHR15
#else
#define PATCHR15 statestr.Reg[15] = ECC | ER15INT | EMODE | R15PC
#endif

#define GETSPSR(bank) bank > 0 ? statestr.Spsr[bank] : ECC | EINT | EMODE;
#define SETPSR(d,s) d = (s) & (ARMword)(CCBITS | INTBITS | MODEBITS)
#define SETINTMODE(d,s) d = ((d) & CCBITS) | ((s) & (INTBITS | MODEBITS))
#define SETCC(d,s) d = ((d) & (INTBITS | MODEBITS)) | ((s) & CCBITS)
#define SETR15PSR(s) if (statestr.Mode == USER26MODE) { \
                        statestr.Reg[15] = ((s) & CCBITS) | R15PC | ER15INT | EMODE; \
                        ASSIGNN((statestr.Reg[15] & NBIT) != 0); \
                        ASSIGNZ((statestr.Reg[15] & ZBIT) != 0); \
                        ASSIGNC((statestr.Reg[15] & CBIT) != 0); \
                        ASSIGNV((statestr.Reg[15] & VBIT) != 0); \
                        } \
                     else { \
                        statestr.Reg[15] = R15PC | ((s) & (CCBITS | R15INTBITS | R15MODEBITS)); \
                        ARMul_R15Altered(emu_state); \
                        }
#define SETABORT(i,m) statestr.Cpsr = ECC | EINT | (i) | (m)

#ifndef MODE32
#define VECTORS 0x20
#define LEGALADDR 0x03ffffff
#define VECTORACCESS(address) (address < VECTORS && ARMul_MODE26BIT && statestr.prog32Sig)
#define ADDREXCEPT(address) (address > LEGALADDR)
#endif

#define INTERNALABORT(address) if (address < VECTORS) \
                                  statestr.Aborted = ARMul_DataAbortV; \
                               else \
                                  statestr.Aborted = ARMul_AddrExceptnV;

#ifdef MODE32
#define TAKEABORT ARMul_Abort(emu_state,ARMul_DataAbortV)
#else
#define TAKEABORT if (statestr.Aborted == ARMul_AddrExceptnV) \
                     ARMul_Abort(emu_state,ARMul_AddrExceptnV); \
                  else \
                     ARMul_Abort(emu_state,ARMul_DataAbortV)
#endif
#define CPTAKEABORT if (!statestr.Aborted) \
                       ARMul_Abort(emu_state,ARMul_UndefinedInstrV); \
                    else if (statestr.Aborted == ARMul_AddrExceptnV) \
                       ARMul_Abort(emu_state,ARMul_AddrExceptnV); \
                    else \
                       ARMul_Abort(emu_state,ARMul_DataAbortV)


/***************************************************************************\
*               Different ways to start the next instruction                *
\***************************************************************************/

#define NORMALCYCLE statestr.NextInstr = SEQ
#define BUSUSEDN statestr.NextInstr |= NONSEQ /* the next fetch will be an N cycle */
#define BUSUSEDINCPCS statestr.Reg[15] += 4; /* a standard PC inc and an S cycle */ \
                      statestr.NextInstr |= PCINCEDSEQ
#define BUSUSEDINCPCN statestr.Reg[15] += 4; /* a standard PC inc and an N cycle */ \
                      statestr.NextInstr |= PCINCEDNONSEQ
#define INCPC statestr.Reg[15] += 4; /* a standard PC inc */ \
                      statestr.NextInstr |=  PCINCEDSEQ
#define FLUSHPIPE statestr.NextInstr |= PRIMEPIPE

/***************************************************************************\
*                          Cycle based emulation                            *
\***************************************************************************/

#define OUTPUTCP(i,a,b)
#define NCYCLE
#define SCYCLE
#define ICYCLE
#define CCYCLE
#define NEXTCYCLE(c)

/***************************************************************************\
*                 States of the cycle based state machine                   *
\***************************************************************************/


/***************************************************************************\
*                 Macros to extract parts of instructions                   *
\***************************************************************************/

#define DESTReg (BITS(12,15))
#define LHSReg (BITS(16,19))
#define RHSReg (BITS(0,3))

#define DEST (statestr.Reg[DESTReg])

#ifdef MODE32
#define LHS (statestr.Reg[LHSReg])
#else
#define LHS ((LHSReg == 15) ? R15PC : (statestr.Reg[LHSReg]) )
#endif

#define MULDESTReg (BITS(16,19))
#define MULLHSReg (BITS(0,3))
#define MULRHSReg (BITS(8,11))
#define MULACCReg (BITS(12,15))

#define DPImmRHS (ARMul_ImmedTable[BITS(0,11)])
#define DPSImmRHS temp = BITS(0,11); \
                  rhs = ARMul_ImmedTable[temp]; \
                  if (temp > 255) /* there was a shift */ \
                     ASSIGNC(rhs >> 31);

#ifdef MODE32
#define DPRegRHS ((BITS(4,11)==0) ? statestr.Reg[RHSReg] \
                                  : GetDPRegRHS(emu_state, instr))
#define DPSRegRHS ((BITS(4,11)==0) ? statestr.Reg[RHSReg] \
                                   : GetDPSRegRHS(emu_state, instr))
#else
#define DPRegRHS ((BITS(0,11)<15) ? statestr.Reg[RHSReg] \
                                  : GetDPRegRHS(emu_state, instr))
#define DPSRegRHS ((BITS(0,11)<15) ? statestr.Reg[RHSReg] \
                                   : GetDPSRegRHS(emu_state, instr))
#endif

#define LSBase statestr.Reg[LHSReg]
#define LSImmRHS (BITS(0,11))

#ifdef MODE32
#define LSRegRHS ((BITS(4,11)==0) ? statestr.Reg[RHSReg] \
                                  : GetLSRegRHS(emu_state, instr))
#else
#define LSRegRHS ((BITS(0,11)<15) ? statestr.Reg[RHSReg] \
                                  : GetLSRegRHS(emu_state, instr))
#endif

#define LSMNumRegs ((ARMword)ARMul_BitList[BITS(0,7)] + \
                    (ARMword)ARMul_BitList[BITS(8,15)] )
#define LSMBaseFirst ((LHSReg == 0 && BIT(0)) || \
                      (BIT(LHSReg) && BITS(0,LHSReg-1) == 0))

#define SWAPSRC (statestr.Reg[RHSReg])

#define LSCOff (BITS(0,7) << 2)
#define CPNum BITS(8,11)

/***************************************************************************\
*                    Macro to rotate n right by b bits                      *
\***************************************************************************/

#define ROTATER(n,b) (((n)>>(b))|((n)<<(32-(b))))

/***************************************************************************\
*                 Macros to store results of instructions                   *
\***************************************************************************/

#define WRITEDEST(d) {/*fprintf(stderr,"WRITEDEST: %d=0x%08x\n",DESTReg,d);*/\
                      if (DESTReg==15) \
                        WriteR15(emu_state, d); \
                     else \
                          DEST = d;\
                      }

#define WRITEDESTNORM(d) {/*fprintf(stderr,"WRITEDEST: %d=0x%08x\n",DESTReg,d);*/ DEST = d;}

#define WRITEDESTPC(d) {/*fprintf(stderr,"WRITEDEST: %d=0x%08x\n", 15, d);*/ WriteR15(emu_state, d);}

#define WRITESDEST(d) { /*fprintf(stderr,"WRITESDEST: %d=0x%08x\n",DESTReg,d);*/\
                      if (DESTReg == 15) \
                         WriteSR15(emu_state, d); \
                      else { \
                         DEST = d; \
                         ARMul_NegZero(emu_state, d); \
                         };\
                      }

#define WRITESDESTNORM(d) {DEST = d; \
                         ARMul_NegZero(emu_state, d); }

#define WRITESDESTPC(d) WriteSR15(emu_state, d) 

#define BYTETOBUS(data) ((data & 0xff) | \
                        ((data & 0xff) << 8) | \
                        ((data & 0xff) << 16) | \
                        ((data & 0xff) << 24))
#define BUSTOBYTE(address,data) \
           if (statestr.bigendSig) \
              temp = (data >> (((address ^ 3) & 3) << 3)) & 0xff; \
           else \
              temp = (data >> ((address & 3) << 3)) & 0xff

#define LOADMULT(instr,address,wb) LoadMult(emu_state,instr,address,wb)
#define LOADSMULT(instr,address,wb) LoadSMult(emu_state,instr,address,wb)
#define STOREMULT(instr,address,wb) StoreMult(emu_state,instr,address,wb)
#define STORESMULT(instr,address,wb) StoreSMult(emu_state,instr,address,wb)

#define POSBRANCH ((instr & 0x7fffff) << 2)
/* DAG: NOTE! Constant in Negbranch was 0xff000000! - obviously wrong -
   corrupts top two bits of the branch offset */
#define NEGBRANCH (0xfc000000 | ((instr & 0xffffff) << 2))

/***************************************************************************\
*                      Stuff that is shared across modes                    *
\***************************************************************************/

void ARMul_Emulate26(void);
void ARMul_Icycles(unsigned number);

extern unsigned ARMul_MultTable[]; /* Number of I cycles for a mult */
extern ARMword ARMul_ImmedTable[]; /* Immediate DP LHS values */
extern char ARMul_BitList[];       /* Number of bits in a byte table */

void ARMul_Abort26(ARMul_State *state, ARMword);
void ARMul_Abort32(ARMul_State *state, ARMword);
unsigned ARMul_NthReg(ARMword instr,unsigned number);
void ARMul_MSRCpsr(ARMul_State *state, ARMword instr, ARMword rhs);
void ARMul_NegZero(ARMul_State *state, ARMword result);
void ARMul_AddCarry(ARMul_State *state, ARMword a, ARMword b, ARMword result);
void ARMul_AddOverflow(ARMul_State *state, ARMword a, ARMword b, ARMword result);
void ARMul_SubCarry(ARMul_State *state, ARMword a, ARMword b, ARMword result);
void ARMul_SubOverflow(ARMul_State *state, ARMword a, ARMword b, ARMword result);
void ARMul_CPSRAltered(ARMul_State *state);
void ARMul_R15Altered(ARMul_State *state);
ARMword ARMul_SwitchMode(ARMul_State *state,ARMword oldmode, ARMword newmode);
unsigned ARMul_NthReg(ARMword instr, unsigned number);
void ARMul_LDC(ARMul_State *state,ARMword instr,ARMword address);
void ARMul_STC(ARMul_State *state,ARMword instr,ARMword address);
void ARMul_MCR(ARMul_State *state,ARMword instr, ARMword source);
ARMword ARMul_MRC(ARMul_State *state,ARMword instr);
void ARMul_CDP(ARMul_State *state,ARMword instr);
unsigned IntPending(ARMul_State *state);
ARMword ARMul_Align(ARMul_State *state, ARMword address, ARMword data);


/***************************************************************************\
*                               ARM Support                                 *
\***************************************************************************/

void ARMul_FixCPSR(ARMul_State *state, ARMword instr, ARMword rhs);
void ARMul_FixSPSR(ARMul_State *state, ARMword instr, ARMword rhs);
void ARMul_UndefInstr(ARMul_State *state,ARMword instr);

#ifdef __riscos__
ARMword GetDPRegRHS(ARMul_State *state, ARMword instr);
ARMword GetDPSRegRHS(ARMul_State *state, ARMword instr);
#endif


#define EVENTLISTSIZE 1024UL

/***************************************************************************\
*                      Macros to scrutinise instructions                    *
\***************************************************************************/


#define UNDEF_Test
#define UNDEF_Shift
#define UNDEF_MSRPC
#define UNDEF_MRSPC
#define UNDEF_MULPCDest
#define UNDEF_MULDestEQOp1
#define UNDEF_LSRBPC
#define UNDEF_LSRBaseEQOffWb
#define UNDEF_LSRBaseEQDestWb
#define UNDEF_LSRPCBaseWb
#define UNDEF_LSRPCOffWb
#define UNDEF_LSMNoRegs
#define UNDEF_LSMPCBase
#define UNDEF_LSMUserBankWb
#define UNDEF_LSMBaseInListWb
#define UNDEF_SWPPC
#define UNDEF_CoProHS
#define UNDEF_MCRPC
#define UNDEF_LSCPCBaseWb
#define UNDEF_UndefNotBounced
#define UNDEF_ShortInt
#define UNDEF_IllegalMode
#define UNDEF_Prog32SigChange
#define UNDEF_Data32SigChange

