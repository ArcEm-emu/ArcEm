/* ################################################################################## */
/* ## Individual decoded instruction functions                                     ## */
/* ################################################################################## */
static void EMFUNCDECL26(BranchForward) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST
  statestr.Reg[15] = pc + 8 + POSBRANCH;
  FLUSHPIPE;
} /* EMFUNCDECL26(BranchForward */

static void EMFUNCDECL26(BranchForwardLink) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST
  statestr.Reg[14] = (pc + 4) | ECC | ER15INT | EMODE; /* put PC into Link */
  statestr.Reg[15] = pc + 8 + POSBRANCH;
  FLUSHPIPE;
} /* EMFUNCDECL26(BranchForwardLink */

static void EMFUNCDECL26(BranchBackward) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST
  statestr.Reg[15] = pc + 8 + NEGBRANCH;
  FLUSHPIPE;
} /* EMFUNCDECL26(BranchBackward */

static void EMFUNCDECL26(BranchBackwardLink) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST
  statestr.Reg[14] = (pc + 4) | ECC | ER15INT | EMODE; /* put PC into Link */
  statestr.Reg[15] = pc + 8 + NEGBRANCH;
  FLUSHPIPE;
} /* EMFUNCDECL26(BranchBackwardLink */

static void EMFUNCDECL26(AndRegMul) (ARMword instr, ARMword pc) {
  register ARMword dest,temp;
  ARMword rhs;

  EMFUNC_CONDTEST
             if (BITS(4,7) == 9) { /* MUL */
                rhs = statestr.Reg[MULRHSReg];
                if (MULLHSReg == MULDESTReg) {
                   statestr.Reg[MULDESTReg] = 0;
                   }
                else if (MULDESTReg != 15)
                   statestr.Reg[MULDESTReg] = statestr.Reg[MULLHSReg] * rhs;
                else {
                   }
                for (dest = 0, temp = 0; dest < 32; dest++)
                   if (rhs & (1L << dest))
                      temp = dest; /* mult takes this many/2 I cycles */
                ARMul_Icycles(ARMul_MultTable[temp]);
                }
             else { /* AND reg */
                rhs = DPRegRHS;
                dest = LHS & rhs;
                WRITEDEST(dest);
                }

} /* EMFUNCDECL26(AndRegMul */

static void EMFUNCDECL26(AndsRegMuls) (ARMword instr, ARMword pc) {
  register ARMword dest,temp;
  ARMword rhs;

  EMFUNC_CONDTEST
            if (BITS(4,7) == 9) { /* MULS */
                rhs = statestr.Reg[MULRHSReg];
                if (MULLHSReg == MULDESTReg) {
                   statestr.Reg[MULDESTReg] = 0;
                   CLEARN;
                   SETZ;
                   }
                else if (MULDESTReg != 15) {
                   dest = statestr.Reg[MULLHSReg] * rhs;
                   ARMul_NegZero(&statestr,dest);
                   statestr.Reg[MULDESTReg] = dest;
                   }
                else {
                   }
                for (dest = 0, temp = 0; dest < 32; dest++)
                   if (rhs & (1L << dest))
                      temp = dest; /* mult takes this many/2 I cycles */
                ARMul_Icycles(ARMul_MultTable[temp]);
                }
             else { /* ANDS reg */
                rhs = DPSRegRHS;
                dest = LHS & rhs;
                WRITESDEST(dest);
                }

} /* EMFUNCDECL26(AndsRegMuls */

static void EMFUNCDECL26(EorRegMla) (ARMword instr, ARMword pc) {
  register ARMword dest,temp;
  ARMword rhs;

  EMFUNC_CONDTEST
          if (BITS(4,7) == 9) { /* MLA */
                rhs = statestr.Reg[MULRHSReg];
                if (MULLHSReg == MULDESTReg) {
                   statestr.Reg[MULDESTReg] = statestr.Reg[MULACCReg];
                   }
                else if (MULDESTReg != 15)
                   statestr.Reg[MULDESTReg] = statestr.Reg[MULLHSReg] * rhs + statestr.Reg[MULACCReg];
                else {
                   }
                for (dest = 0, temp = 0; dest < 32; dest++)
                   if (rhs & (1L << dest))
                      temp = dest; /* mult takes this many/2 I cycles */
                ARMul_Icycles(ARMul_MultTable[temp]);
                }
             else {
                rhs = DPRegRHS;
                dest = LHS ^ rhs;
                WRITEDEST(dest);
                }

} /* EMFUNCDECL26(EorRegMla */

static void EMFUNCDECL26(EorsRegMlas) (ARMword instr, ARMword pc) {
  register ARMword dest,temp;
  ARMword rhs;

  EMFUNC_CONDTEST
            if (BITS(4,7) == 9) { /* MLAS */
                rhs = statestr.Reg[MULRHSReg];
                if (MULLHSReg == MULDESTReg) {
                   dest = statestr.Reg[MULACCReg];
                   ARMul_NegZero(&statestr,dest);
                   statestr.Reg[MULDESTReg] = dest;
                   }
                else if (MULDESTReg != 15) {
                   dest = statestr.Reg[MULLHSReg] * rhs + statestr.Reg[MULACCReg];
                   ARMul_NegZero(&statestr,dest);
                   statestr.Reg[MULDESTReg] = dest;
                   }
                else {
                   }
                for (dest = 0, temp = 0; dest < 32; dest++)
                   if (rhs & (1L << dest))
                      temp = dest; /* mult takes this many/2 I cycles */
                ARMul_Icycles(ARMul_MultTable[temp]);
                }
             else { /* EORS Reg */
                rhs = DPSRegRHS;
                dest = LHS ^ rhs;
                WRITESDEST(dest);
                }

} /* EMFUNCDECL26(EorsRegMlas */

static void EMFUNCDECL26(SubReg) (ARMword instr, ARMword pc) {
  register ARMword dest;
  ARMword rhs;

  EMFUNC_CONDTEST
  rhs = DPRegRHS;
  dest = LHS - rhs;
  WRITEDEST(dest);
} /* EMFUNCDECL26(SubReg */

static void EMFUNCDECL26(SubsReg) (ARMword instr, ARMword pc) {
  register ARMword dest;
  ARMword lhs,rhs;

  EMFUNC_CONDTEST
          lhs = LHS;
             rhs = DPRegRHS;
             dest = lhs - rhs;
             if ((lhs >= rhs) || ((rhs | lhs) >> 31)) {
                ARMul_SubCarry(&statestr,lhs,rhs,dest);
                ARMul_SubOverflow(&statestr,lhs,rhs,dest);
                }
             else {
                CLEARC;
                CLEARV;
                }
             WRITESDEST(dest);

} /* EMFUNCDECL26(SubsReg */

static void EMFUNCDECL26(RsbReg) (ARMword instr, ARMword pc) {
  register ARMword dest;
  ARMword rhs;

  EMFUNC_CONDTEST
  rhs = DPRegRHS;
  dest = rhs - LHS;
  WRITEDEST(dest);
} /* EMFUNCDECL26(RsbReg */

static void EMFUNCDECL26(RsbsReg) (ARMword instr, ARMword pc) {
  register ARMword dest;
  ARMword lhs,rhs;

  EMFUNC_CONDTEST
          lhs = LHS;
             rhs = DPRegRHS;
             dest = rhs - lhs;
             if ((rhs >= lhs) || ((rhs | lhs) >> 31)) {
                ARMul_SubCarry(&statestr,rhs,lhs,dest);
                ARMul_SubOverflow(&statestr,rhs,lhs,dest);
                }
             else {
                CLEARC;
                CLEARV;
                }
             WRITESDEST(dest);

} /* EMFUNCDECL26(RsbsReg */

static void EMFUNCDECL26(AddReg) (ARMword instr, ARMword pc) {
  register ARMword dest;
  ARMword rhs;

  EMFUNC_CONDTEST
         rhs = DPRegRHS;
             dest = LHS + rhs;
             WRITEDEST(dest);

} /* EMFUNCDECL26(AddReg */

static void EMFUNCDECL26(AddsReg) (ARMword instr, ARMword pc) {
  register ARMword dest;
  ARMword lhs,rhs;

  EMFUNC_CONDTEST
         lhs = LHS;
             rhs = DPRegRHS;
             dest = lhs + rhs;
             ASSIGNZ(dest==0);
             if ((lhs | rhs) >> 30) { /* possible C,V,N to set */
                ASSIGNN(NEG(dest));
                ARMul_AddCarry(&statestr,lhs,rhs,dest);
                ARMul_AddOverflow(&statestr,lhs,rhs,dest);
                }
             else {
                CLEARN;
                CLEARC;
                CLEARV;
                }
             WRITESDEST(dest);

} /* EMFUNCDECL26(AddsReg */

static void EMFUNCDECL26(AdcReg) (ARMword instr, ARMword pc) {
  register ARMword dest;
  ARMword rhs;

  EMFUNC_CONDTEST
rhs = DPRegRHS;
             dest = LHS + rhs + CFLAG;
             WRITEDEST(dest);

} /* EMFUNCDECL26(AdcReg */

static void EMFUNCDECL26(AdcsReg) (ARMword instr, ARMword pc) {
  register ARMword dest;
  ARMword lhs,rhs;

  EMFUNC_CONDTEST

  lhs = LHS;
             rhs = DPRegRHS;
             dest = lhs + rhs + CFLAG;
             ASSIGNZ(dest==0);
             if ((lhs | rhs) >> 30) { /* possible C,V,N to set */
                ASSIGNN(NEG(dest));
                ARMul_AddCarry(&statestr,lhs,rhs,dest);
                ARMul_AddOverflow(&statestr,lhs,rhs,dest);
                }
             else {
                CLEARN;
                CLEARC;
                CLEARV;
                }
             WRITESDEST(dest);

} /* EMFUNCDECL26(AdcsReg */

static void EMFUNCDECL26(SbcReg) (ARMword instr, ARMword pc) {
  register ARMword dest;
  ARMword rhs;

  EMFUNC_CONDTEST
 rhs = DPRegRHS;
             dest = LHS - rhs - !CFLAG;
             WRITEDEST(dest);

} /* EMFUNCDECL26(SbcReg */

static void EMFUNCDECL26(SbcsReg) (ARMword instr, ARMword pc) {
  register ARMword dest;
  ARMword lhs,rhs;

  EMFUNC_CONDTEST
 lhs = LHS;
             rhs = DPRegRHS;
             dest = lhs - rhs - !CFLAG;
             if ((lhs >= rhs) || ((rhs | lhs) >> 31)) {
                ARMul_SubCarry(&statestr,lhs,rhs,dest);
                ARMul_SubOverflow(&statestr,lhs,rhs,dest);
                }
             else {
                CLEARC;
                CLEARV;
                }
             WRITESDEST(dest);

} /* EMFUNCDECL26(SbcsReg */

static void EMFUNCDECL26(RscReg) (ARMword instr, ARMword pc) {
  ARMword dest,rhs;

  EMFUNC_CONDTEST
  rhs = DPRegRHS;
             dest = rhs - LHS - !CFLAG;
             WRITEDEST(dest);

} /* EMFUNCDECL26(RscReg */

static void EMFUNCDECL26(RscsReg) (ARMword instr, ARMword pc) {
  register ARMword dest;
  ARMword lhs,rhs;

  EMFUNC_CONDTEST
    lhs = LHS;
             rhs = DPRegRHS;
             dest = rhs - lhs - !CFLAG;
             if ((rhs >= lhs) || ((rhs | lhs) >> 31)) {
                ARMul_SubCarry(&statestr,rhs,lhs,dest);
                ARMul_SubOverflow(&statestr,rhs,lhs,dest);
                }
             else {
                CLEARC;
                CLEARV;
                }
             WRITESDEST(dest);

} /* EMFUNCDECL26(RscsReg */

static void EMFUNCDECL26(TstRegMrs1SwpNorm) (ARMword instr, ARMword pc) {
  register ARMword dest, temp;

  EMFUNC_CONDTEST
             if (BITS(4,11) == 9) { /* SWP */
                temp = LHS;
                BUSUSEDINCPCS;

                if (ADDREXCEPT(temp)) {
                   dest = 0; /* Stop GCC warning, not sure what's appropriate */
                   INTERNALABORT(temp);
                   (void)ARMul_LoadWordN(&statestr,temp);
                   (void)ARMul_LoadWordN(&statestr,temp);

                } else {
                  dest = ARMul_SwapWord(&statestr,temp,statestr.Reg[RHSReg]);
                }

                if (temp & 3) {
                  DEST = ARMul_Align(&statestr,temp,dest);
                } else {
                  DEST = dest;
                }

                if (statestr.abortSig || statestr.Aborted) {
                  TAKEABORT;
                }

             } else if ((BITS(0,11)==0) && (LHSReg==15)) { /* MRS CPSR */
               DEST = ECC | EINT | EMODE;

             }

} /* EMFUNCDECL26( */

static void EMFUNCDECL26(TstpRegNorm) (ARMword instr, ARMword pc) {
  register ARMword dest;
  ARMword rhs;

  EMFUNC_CONDTEST
  rhs = DPSRegRHS;
  dest = LHS & rhs;
  ARMul_NegZero(&statestr,dest);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(TeqpRegNorm) (ARMword instr, ARMword pc) {
  register ARMword dest;
  ARMword rhs;
  EMFUNC_CONDTEST

  rhs = DPSRegRHS;
  dest = LHS ^ rhs;
  ARMul_NegZero(&statestr,dest);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(CmpRegMrs2SwpNorm) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST

             if (BITS(4,11) == 9) { /* SWP */
                temp = LHS;
                BUSUSEDINCPCS;
                if (ADDREXCEPT(temp)) {
                   INTERNALABORT(temp);
                   (void)ARMul_LoadByte(&statestr,temp);
                   (void)ARMul_LoadByte(&statestr,temp);
                   }
                else
                DEST = ARMul_SwapByte(&statestr,temp,statestr.Reg[RHSReg]);
                if (statestr.abortSig || statestr.Aborted) {
                   TAKEABORT;
                   }
                }
             else if ((BITS(0,11)==0) && (LHSReg==15)) { /* MRS SPSR */
                DEST = GETSPSR(statestr.Bank);
                }
             else {
                }

} /* EMFUNCDECL26( */

static void EMFUNCDECL26(CmppRegNorm) (ARMword instr, ARMword pc) {
  register ARMword dest;
  ARMword lhs,rhs;
  EMFUNC_CONDTEST

  lhs = LHS;
  rhs = DPRegRHS;
  dest = lhs - rhs;
  ARMul_NegZero(&statestr,dest);
  if ((lhs >= rhs) || ((rhs | lhs) >> 31)) {
     ARMul_SubCarry(&statestr,lhs,rhs,dest);
     ARMul_SubOverflow(&statestr,lhs,rhs,dest);
     }
  else {
     CLEARC;
     CLEARV;
                   }
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(CmnpRegNorm) (ARMword instr, ARMword pc) {
  register ARMword dest;
  ARMword lhs,rhs;
  EMFUNC_CONDTEST

  lhs = LHS;
  rhs = DPRegRHS;
  dest = lhs + rhs;
  ASSIGNZ(dest==0);
  if ((lhs | rhs) >> 30) { /* possible C,V,N to set */
     ASSIGNN(NEG(dest));
     ARMul_AddCarry(&statestr,lhs,rhs,dest);
     ARMul_AddOverflow(&statestr,lhs,rhs,dest);
  } else {
    CLEARN;
    CLEARC;
    CLEARV;
  }
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(OrrRegNorm) (ARMword instr, ARMword pc) {
  register ARMword dest;
  ARMword rhs;
  EMFUNC_CONDTEST

  rhs = DPRegRHS;
  dest = LHS | rhs;
  WRITEDESTNORM(dest);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(OrrsRegNorm) (ARMword instr, ARMword pc) {
  register ARMword dest;
  ARMword rhs;

  EMFUNC_CONDTEST
  rhs = DPSRegRHS;
  dest = LHS | rhs;
  WRITESDESTNORM(dest);
} /* EMFUNCDECL26( */

/* Eventually, for all ARM */
#ifdef __riscos__
void EMFUNCDECL26(MovRegNorm) (ARMword instr, ARMword pc);
void EMFUNCDECL26(MovsRegNorm) (ARMword instr, ARMword pc);
#else
static void EMFUNCDECL26(MovRegNorm) (ARMword instr, ARMword pc) {
  register ARMword dest;

  EMFUNC_CONDTEST
  dest = DPRegRHS;
  WRITEDESTNORM(dest);
} /* EMFUNCDECL26(MovReg */

static void EMFUNCDECL26(MovsRegNorm) (ARMword instr, ARMword pc) {
  register ARMword dest;

  EMFUNC_CONDTEST
  dest = DPSRegRHS;
  WRITESDESTNORM(dest);
} /* EMFUNCDECL26(MovsReg */
#endif

static void EMFUNCDECL26(BicRegNorm) (ARMword instr, ARMword pc) {
  register ARMword dest;
  ARMword rhs;

  EMFUNC_CONDTEST
  rhs = DPRegRHS;
  dest = LHS & ~rhs;
  WRITEDESTNORM(dest);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(BicsRegNorm) (ARMword instr, ARMword pc) {
  register ARMword dest;
  ARMword rhs;

  EMFUNC_CONDTEST
  rhs = DPSRegRHS;
  dest = LHS & ~rhs;
  WRITESDESTNORM(dest);
} /* EMFUNCDECL26( */

#ifdef __riscos__
void EMFUNCDECL26(MvnRegNorm) (ARMword instr, ARMword pc);
void EMFUNCDECL26(MvnsRegNorm) (ARMword instr, ARMword pc);
#else
static void EMFUNCDECL26(MvnRegNorm) (ARMword instr, ARMword pc) {
  register ARMword dest;

  EMFUNC_CONDTEST
  dest = ~DPRegRHS;
  WRITEDESTNORM(dest);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(MvnsRegNorm) (ARMword instr, ARMword pc) {
  register ARMword dest;

  EMFUNC_CONDTEST
  dest = ~DPSRegRHS;
  WRITESDESTNORM(dest);
} /* EMFUNCDECL26( */
#endif

static void EMFUNCDECL26(TstRegMrs1SwpPC) (ARMword instr, ARMword pc) {
  register ARMword dest, temp;

  EMFUNC_CONDTEST
             if (BITS(4,11) == 9) { /* SWP */
                temp = LHS;
                BUSUSEDINCPCS;
#ifndef MODE32
                if (ADDREXCEPT(temp)) {
                   dest = 0; /* Stop GCC warning, not sure what's appropriate */
                   INTERNALABORT(temp);
                   (void)ARMul_LoadWordN(&statestr,temp);
                   (void)ARMul_LoadWordN(&statestr,temp);
                   }
                else
#endif
                dest = ARMul_SwapWord(&statestr,temp,statestr.Reg[RHSReg]);
                if (temp & 3)
                    DEST = ARMul_Align(&statestr,temp,dest);
                else
                    DEST = dest;
                if (statestr.abortSig || statestr.Aborted) {
                   TAKEABORT;
                   }
                }
             else if ((BITS(0,11)==0) && (LHSReg==15)) { /* MRS CPSR */
                DEST = ECC | EINT | EMODE;
                }
             else {
                }

} /* EMFUNCDECL26( */

static void EMFUNCDECL26(TstpRegPC) (ARMword instr, ARMword pc) {
  register ARMword temp;
  ARMword rhs;

  EMFUNC_CONDTEST
  rhs = DPRegRHS;
  temp = LHS & rhs;
  SETR15PSR(temp);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(TeqRegMsr1PC) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST
            if (BITS(17,18)==0) { /* MSR reg to CPSR */
                temp = DPRegRHS;
                   ARMul_FixCPSR(&statestr,instr,temp);
                }
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(TeqpRegPC) (ARMword instr, ARMword pc) {
  register ARMword temp;
  ARMword rhs;
  EMFUNC_CONDTEST

  rhs = DPRegRHS;
  temp = LHS ^ rhs;
  SETR15PSR(temp);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(CmpRegMrs2SwpPC) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST

             if (BITS(4,11) == 9) { /* SWP */
                temp = LHS;
                BUSUSEDINCPCS;
#ifndef MODE32
                if (ADDREXCEPT(temp)) {
                   INTERNALABORT(temp);
                   (void)ARMul_LoadByte(&statestr,temp);
                   (void)ARMul_LoadByte(&statestr,temp);
                   }
                else
#endif
                DEST = ARMul_SwapByte(&statestr,temp,statestr.Reg[RHSReg]);
                if (statestr.abortSig || statestr.Aborted) {
                   TAKEABORT;
                   }
                }
             else if ((BITS(0,11)==0) && (LHSReg==15)) { /* MRS SPSR */
                DEST = GETSPSR(statestr.Bank);
                }
             else {
                }

} /* EMFUNCDECL26( */

static void EMFUNCDECL26(CmppRegPC) (ARMword instr, ARMword pc) {
  register ARMword temp;
  ARMword rhs;
  EMFUNC_CONDTEST

  rhs = DPRegRHS;
  temp = LHS - rhs;
  SETR15PSR(temp);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(CmnRegMSRreg2PC) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST
  if (BITS(17,18)==0) { /* MSR */
    ARMul_FixSPSR(&statestr,instr,DPRegRHS);
  } else {
  }
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(CmnpRegPC) (ARMword instr, ARMword pc) {
  register ARMword temp;
  ARMword rhs;
  EMFUNC_CONDTEST

  rhs = DPRegRHS;
  temp = LHS + rhs;
  SETR15PSR(temp);

} /* EMFUNCDECL26( */

static void EMFUNCDECL26(OrrRegPC) (ARMword instr, ARMword pc) {
  register ARMword dest;
  ARMword rhs;
  EMFUNC_CONDTEST

  rhs = DPRegRHS;
  dest = LHS | rhs;
  WRITEDESTPC(dest);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(OrrsRegPC) (ARMword instr, ARMword pc) {
  register ARMword dest;
  ARMword rhs;

  EMFUNC_CONDTEST
  rhs = DPSRegRHS;
  dest = LHS | rhs;
  WRITESDESTPC(dest);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(MovRegPC) (ARMword instr, ARMword pc) {
  register ARMword dest;

  EMFUNC_CONDTEST
  dest = DPRegRHS;
  WRITEDESTPC(dest);
} /* EMFUNCDECL26(MovReg */

static void EMFUNCDECL26(MovsRegPC) (ARMword instr, ARMword pc) {
  register ARMword dest;

  EMFUNC_CONDTEST
  dest = DPSRegRHS;
  WRITESDESTPC(dest);
} /* EMFUNCDECL26(MovsReg */

static void EMFUNCDECL26(BicRegPC) (ARMword instr, ARMword pc) {
  register ARMword dest;
  ARMword rhs;

  EMFUNC_CONDTEST
  rhs = DPRegRHS;
  dest = LHS & ~rhs;
  WRITEDESTPC(dest);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(BicsRegPC) (ARMword instr, ARMword pc) {
  register ARMword dest;
  ARMword rhs;

  EMFUNC_CONDTEST
  rhs = DPSRegRHS;
  dest = LHS & ~rhs;
  WRITESDESTPC(dest);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(MvnRegPC) (ARMword instr, ARMword pc) {
  register ARMword dest;

  EMFUNC_CONDTEST
  dest = ~DPRegRHS;
  WRITEDESTPC(dest);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(MvnsRegPC) (ARMword instr, ARMword pc) {
  register ARMword dest;

  EMFUNC_CONDTEST
  dest = ~DPSRegRHS;
  WRITESDESTPC(dest);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(AndImm) (ARMword instr, ARMword pc) {
  register ARMword dest;

  EMFUNC_CONDTEST
  dest = LHS & DPImmRHS;
  WRITEDEST(dest);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(AndsImm) (ARMword instr, ARMword pc) {
  register ARMword dest,rhs,temp;

  EMFUNC_CONDTEST
  DPSImmRHS;
  dest = LHS & rhs;
  WRITESDEST(dest);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(EorImm) (ARMword instr, ARMword pc) {
  register ARMword dest;

  EMFUNC_CONDTEST
  dest = LHS ^ DPImmRHS;
  WRITEDEST(dest);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(EorsImm) (ARMword instr, ARMword pc) {
  register ARMword dest,rhs,temp;

  EMFUNC_CONDTEST
  DPSImmRHS;
  dest = LHS ^ rhs;
  WRITESDEST(dest);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(SubImm) (ARMword instr, ARMword pc) {
  register ARMword dest;

  EMFUNC_CONDTEST
  dest = LHS - DPImmRHS;
  WRITEDEST(dest);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(SubsImmNorm) (ARMword instr, ARMword pc) {
  register ARMword dest,rhs,lhs;

  EMFUNC_CONDTEST
             lhs = LHS;
             rhs = DPImmRHS;
             dest = lhs - rhs;
             if ((lhs >= rhs) || ((rhs | lhs) >> 31)) {
                ARMul_SubCarry(&statestr,lhs,rhs,dest);
                ARMul_SubOverflow(&statestr,lhs,rhs,dest);
                }  
             else {
                CLEARC;
                CLEARV;
                }

             if (DESTReg == 15)
                WRITESDESTPC(dest);
             else
                WRITESDESTNORM(dest);

} /* EMFUNCDECL26( */

/*static void EMFUNCDECL26(SubsImmPc) (ARMword instr, ARMword pc) {
  register ARMword dest,rhs,lhs;

  EMFUNC_CONDTEST
             lhs = LHS;
             rhs = DPImmRHS;
             dest = lhs - rhs;
             if ((lhs >= rhs) || ((rhs | lhs) >> 31)) {
                ARMul_SubCarry(&statestr,lhs,rhs,dest);
                ARMul_SubOverflow(&statestr,lhs,rhs,dest);
                }  
             else {
                CLEARC;
                CLEARV;
                }
             WRITESDESTPC(dest);

}*/ /* EMFUNCDECL26( */

static void EMFUNCDECL26(RsbImm) (ARMword instr, ARMword pc) {
  register ARMword dest;

  EMFUNC_CONDTEST
  dest = DPImmRHS - LHS;
  WRITEDEST(dest);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(RsbsImm) (ARMword instr, ARMword pc) {
  register ARMword dest,rhs,lhs;

  EMFUNC_CONDTEST
            lhs = LHS;
             rhs = DPImmRHS;
             dest = rhs - lhs;
             if ((rhs >= lhs) || ((rhs | lhs) >> 31)) {
                ARMul_SubCarry(&statestr,rhs,lhs,dest);
                ARMul_SubOverflow(&statestr,rhs,lhs,dest);
                }
             else {
                CLEARC;
                CLEARV;
                }
             WRITESDEST(dest);

} /* EMFUNCDECL26( */

static void EMFUNCDECL26(AddImm) (ARMword instr, ARMword pc) {
  register ARMword dest;

  EMFUNC_CONDTEST
  dest = LHS + DPImmRHS;
  WRITEDEST(dest);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(AddsImm) (ARMword instr, ARMword pc) {
  register ARMword dest,lhs,rhs;

  EMFUNC_CONDTEST
            lhs = LHS;
             rhs = DPImmRHS;
             dest = lhs + rhs;
             ASSIGNZ(dest==0);
             if ((lhs | rhs) >> 30) { /* possible C,V,N to set */
                ASSIGNN(NEG(dest));
                ARMul_AddCarry(&statestr,lhs,rhs,dest);
                ARMul_AddOverflow(&statestr,lhs,rhs,dest);
                }
             else {
                CLEARN;
                CLEARC;
                CLEARV;
                }
             WRITESDEST(dest);

} /* EMFUNCDECL26( */

static void EMFUNCDECL26(AdcImm) (ARMword instr, ARMword pc) {
  register ARMword dest;

  EMFUNC_CONDTEST
  dest = LHS + DPImmRHS + CFLAG;
  WRITEDEST(dest);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(AdcsImm) (ARMword instr, ARMword pc) {
  register ARMword dest,lhs,rhs;

  EMFUNC_CONDTEST
           lhs = LHS;
             rhs = DPImmRHS;
             dest = lhs + rhs + CFLAG;
             ASSIGNZ(dest==0);
             if ((lhs | rhs) >> 30) { /* possible C,V,N to set */
                ASSIGNN(NEG(dest));
                ARMul_AddCarry(&statestr,lhs,rhs,dest);
                ARMul_AddOverflow(&statestr,lhs,rhs,dest);
                }
             else {
                CLEARN;
                CLEARC;
                CLEARV;
                }
             WRITESDEST(dest);

} /* EMFUNCDECL26( */

static void EMFUNCDECL26(SbcImm) (ARMword instr, ARMword pc) {
  register ARMword dest;

  EMFUNC_CONDTEST
  dest = LHS - DPImmRHS - !CFLAG;
  WRITEDEST(dest);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(SbcsImm) (ARMword instr, ARMword pc) {
  register ARMword dest,lhs,rhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             rhs = DPImmRHS;
             dest = lhs - rhs - !CFLAG;
             if ((lhs >= rhs) || ((rhs | lhs) >> 31)) {
                ARMul_SubCarry(&statestr,lhs,rhs,dest);
                ARMul_SubOverflow(&statestr,lhs,rhs,dest);
                }
             else {
                CLEARC;
                CLEARV;
                }
             WRITESDEST(dest);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(RscImm) (ARMword instr, ARMword pc) {
  register ARMword dest;

  EMFUNC_CONDTEST
  dest = DPImmRHS - LHS - !CFLAG;
  WRITEDEST(dest);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(RscsImm) (ARMword instr, ARMword pc) {
  register ARMword dest,lhs,rhs;
  EMFUNC_CONDTEST

            lhs = LHS;
             rhs = DPImmRHS;
             dest = rhs - lhs - !CFLAG;
             if ((rhs >= lhs) || ((rhs | lhs) >> 31)) {
                ARMul_SubCarry(&statestr,rhs,lhs,dest);
                ARMul_SubOverflow(&statestr,rhs,lhs,dest);
                }
             else {
                CLEARC;
                CLEARV;
                }
             WRITESDEST(dest);

} /* EMFUNCDECL26( */

static void EMFUNCDECL26(TstImm) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST

} /* EMFUNCDECL26( */

static void EMFUNCDECL26(TstpImm) (ARMword instr, ARMword pc) {
  register ARMword dest,rhs,temp;
  EMFUNC_CONDTEST

             if (DESTReg == 15) { /* TSTP immed */
#ifdef MODE32
                statestr.Cpsr = GETSPSR(statestr.Bank);
                ARMul_CPSRAltered(state);
#else
                temp = LHS & DPImmRHS;
                SETR15PSR(temp);
#endif
                }
             else {
                DPSImmRHS; /* TST immed */
                dest = LHS & rhs;
                ARMul_NegZero(&statestr,dest);
                }

} /* EMFUNCDECL26( */

static void EMFUNCDECL26(TeqImmMsr1) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST

             if (DESTReg==15 && BITS(17,18)==0) { /* MSR immed to CPSR */
                ARMul_FixCPSR(&statestr,instr,DPImmRHS);  
                }
             else {
                }

} /* EMFUNCDECL26( */

static void EMFUNCDECL26(TeqpImm) (ARMword instr, ARMword pc) {
  register ARMword dest,rhs,temp;
  EMFUNC_CONDTEST

             if (DESTReg == 15) { /* TEQP immed */
#ifdef MODE32
                statestr.Cpsr = GETSPSR(statestr.Bank);
                ARMul_CPSRAltered(state);
#else
                temp = LHS ^ DPImmRHS;
                SETR15PSR(temp);
#endif
                }
             else {
                DPSImmRHS; /* TEQ immed */
                dest = LHS ^ rhs;
                ARMul_NegZero(&statestr,dest);
                }

} /* EMFUNCDECL26( */

static void EMFUNCDECL26(CmpImm) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(CmppImm) (ARMword instr, ARMword pc) {
  register ARMword dest,lhs,rhs,temp;
  EMFUNC_CONDTEST

             if (DESTReg == 15) { /* CMPP immed */
#ifdef MODE32
                statestr.Cpsr = GETSPSR(statestr.Bank);
                ARMul_CPSRAltered(state);
#else
                temp = LHS - DPImmRHS;
                SETR15PSR(temp);
#endif
                }
             else {
                lhs = LHS; /* CMP immed */
                rhs = DPImmRHS;
                dest = lhs - rhs;
                ARMul_NegZero(&statestr,dest);
                if ((lhs >= rhs) || ((rhs | lhs) >> 31)) {
                   ARMul_SubCarry(&statestr,lhs,rhs,dest);
                   ARMul_SubOverflow(&statestr,lhs,rhs,dest);
                   }
                else {
                   CLEARC;
                   CLEARV;
                   }
                }

} /* EMFUNCDECL26( */

static void EMFUNCDECL26(CmnImmMsr2) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST
            if (DESTReg==15 && BITS(17,18)==0) /* MSR */
                ARMul_FixSPSR(&statestr, instr, DPImmRHS);
             else {
                }

} /* EMFUNCDECL26( */

static void EMFUNCDECL26(CmnpImm) (ARMword instr, ARMword pc) {
  register ARMword dest,lhs,rhs,temp;
  EMFUNC_CONDTEST


             if (DESTReg == 15) { /* CMNP immed */
#ifdef MODE32
                statestr.Cpsr = GETSPSR(statestr.Bank);
                ARMul_CPSRAltered(state);
#else
                temp = LHS + DPImmRHS;
                SETR15PSR(temp);
#endif
                }
             else {
                lhs = LHS; /* CMN immed */
                rhs = DPImmRHS;
                dest = lhs + rhs;
                ASSIGNZ(dest==0);
                if ((lhs | rhs) >> 30) { /* possible C,V,N to set */
                   ASSIGNN(NEG(dest));
                   ARMul_AddCarry(&statestr,lhs,rhs,dest);
                   ARMul_AddOverflow(&statestr,lhs,rhs,dest);
                   }
                else {
                   CLEARN;
                   CLEARC;
                   CLEARV;
                   }
                }

} /* EMFUNCDECL26( */

static void EMFUNCDECL26(OrrImm) (ARMword instr, ARMword pc) {
  register ARMword dest;
  EMFUNC_CONDTEST

  dest = LHS | DPImmRHS;
  WRITEDEST(dest);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(OrrsImm) (ARMword instr, ARMword pc) {
  register ARMword dest,rhs,temp;
  EMFUNC_CONDTEST

  DPSImmRHS;
  dest = LHS | rhs;
  WRITESDEST(dest);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(MovImm) (ARMword instr, ARMword pc) {
  register ARMword dest;
  EMFUNC_CONDTEST

  dest = DPImmRHS;
  WRITEDEST(dest);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(MovsImm) (ARMword instr, ARMword pc) {
  register ARMword rhs,temp;
  EMFUNC_CONDTEST

  DPSImmRHS;
  WRITESDEST(rhs);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(BicImm) (ARMword instr, ARMword pc) {
  register ARMword dest;
  EMFUNC_CONDTEST

  dest = LHS & ~DPImmRHS;
  WRITEDEST(dest);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(BicsImm) (ARMword instr, ARMword pc) {
  register ARMword dest,rhs,temp;
  EMFUNC_CONDTEST

  DPSImmRHS;
  dest = LHS & ~rhs;
  WRITESDEST(dest);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(MvnImm) (ARMword instr, ARMword pc) {
  register ARMword dest;
  EMFUNC_CONDTEST

  dest = ~DPImmRHS;
  WRITEDEST(dest);
} /* EMFUNCDECL26( */

static void EMFUNCDECL26(MvnsImm) (ARMword instr, ARMword pc) {
  register ARMword rhs,temp;
  EMFUNC_CONDTEST

  DPSImmRHS;
  WRITESDEST(~rhs);
} /* EMFUNCDECL26( */


static void EMFUNCDECL26(StoreNoWritePostDecImm) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             if (StoreWord(&statestr,instr,lhs))
                LSBase = lhs - LSImmRHS;
}

static void EMFUNCDECL26(LoadNoWritePostDecImm) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             if (LoadWord(&statestr,instr,lhs))
                LSBase = lhs - LSImmRHS;
}

static void EMFUNCDECL26(StoreWritePostDecImm) (ARMword instr, ARMword pc) {
  register ARMword lhs,temp;
  EMFUNC_CONDTEST
             lhs = LHS;
             temp = lhs - LSImmRHS;
             statestr.NtransSig = LOW;
             if (StoreWord(&statestr,instr,lhs))
                LSBase = temp;
             statestr.NtransSig = (statestr.Mode & 3)?HIGH:LOW;
}

static void EMFUNCDECL26(LoadWritePostDecImm) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             statestr.NtransSig = LOW;
             if (LoadWord(&statestr,instr,lhs))
                LSBase = lhs - LSImmRHS;
             statestr.NtransSig = (statestr.Mode & 3)?HIGH:LOW;
}

static void EMFUNCDECL26(StoreBNoWritePostDecImm) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             if (StoreByte(&statestr,instr,lhs))
                LSBase = lhs - LSImmRHS;
}

static void EMFUNCDECL26(LoadBNoWritePostDecImm) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             if (LoadByte(&statestr,instr,lhs))
                LSBase = lhs - LSImmRHS;
}

static void EMFUNCDECL26(StoreBWritePostDecImm) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             statestr.NtransSig = LOW;
             if (StoreByte(&statestr,instr,lhs))
                LSBase = lhs - LSImmRHS;
             statestr.NtransSig = (statestr.Mode & 3)?HIGH:LOW;
}

static void EMFUNCDECL26(LoadBWritePostDecImm) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             statestr.NtransSig = LOW;
             if (LoadByte(&statestr,instr,lhs))
                LSBase = lhs - LSImmRHS;
             statestr.NtransSig = (statestr.Mode & 3)?HIGH:LOW;
}

static void EMFUNCDECL26(StoreNoWritePostIncImm) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             if (StoreWord(&statestr,instr,lhs))
                LSBase = lhs + LSImmRHS;
}

static void EMFUNCDECL26(LoadNoWritePostIncImm) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             if (LoadWord(&statestr,instr,lhs))
                LSBase = lhs + LSImmRHS;
}

static void EMFUNCDECL26(StoreWritePostIncImm) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             statestr.NtransSig = LOW;
             if (StoreWord(&statestr,instr,lhs))
                LSBase = lhs + LSImmRHS;
             statestr.NtransSig = (statestr.Mode & 3)?HIGH:LOW;
}

static void EMFUNCDECL26(LoadWritePostIncImm) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             statestr.NtransSig = LOW;
             if (LoadWord(&statestr,instr,lhs))
                LSBase = lhs + LSImmRHS;
             statestr.NtransSig = (statestr.Mode & 3)?HIGH:LOW;
}

static void EMFUNCDECL26(StoreBNoWritePostIncImm) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             if (StoreByte(&statestr,instr,lhs))
                LSBase = lhs + LSImmRHS;
}

static void EMFUNCDECL26(LoadBNoWritePostIncImm) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             if (LoadByte(&statestr,instr,lhs))
                LSBase = lhs + LSImmRHS;
}

static void EMFUNCDECL26(StoreBWritePostIncImm) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             statestr.NtransSig = LOW;
             if (StoreByte(&statestr,instr,lhs))
                LSBase = lhs + LSImmRHS;
             statestr.NtransSig = (statestr.Mode & 3)?HIGH:LOW;
}

static void EMFUNCDECL26(LoadBWritePostIncImm) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             statestr.NtransSig = LOW;
             if (LoadByte(&statestr,instr,lhs))
                LSBase = lhs + LSImmRHS;
             statestr.NtransSig = (statestr.Mode & 3)?HIGH:LOW;
}


static void EMFUNCDECL26(StoreNoWritePreDecImm) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST
             (void)StoreWord(&statestr,instr,LHS - LSImmRHS);
}

static void EMFUNCDECL26(LoadNoWritePreDecImm) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST
             (void)LoadWord(&statestr,instr,LHS - LSImmRHS);
}

static void EMFUNCDECL26(StoreWritePreDecImm) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST
             temp = LHS - LSImmRHS;
             if (StoreWord(&statestr,instr,temp))
                LSBase = temp;
}

static void EMFUNCDECL26(LoadWritePreDecImm) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST
             temp = LHS - LSImmRHS;
             if (LoadWord(&statestr,instr,temp))
                LSBase = temp;
}

static void EMFUNCDECL26(StoreBNoWritePreDecImm) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST
             (void)StoreByte(&statestr,instr,LHS - LSImmRHS);
}

static void EMFUNCDECL26(LoadBNoWritePreDecImm) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST
             (void)LoadByte(&statestr,instr,LHS - LSImmRHS);
}

static void EMFUNCDECL26(StoreBWritePreDecImm) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST
             temp = LHS - LSImmRHS;
             if (StoreByte(&statestr,instr,temp))
                LSBase = temp;
}

static void EMFUNCDECL26(LoadBWritePreDecImm) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST
             temp = LHS - LSImmRHS;
             if (LoadByte(&statestr,instr,temp))
                LSBase = temp;
}

static void EMFUNCDECL26(StoreNoWritePreIncImm) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST
             (void)StoreWord(&statestr,instr,LHS + LSImmRHS);
}

static void EMFUNCDECL26(LoadNoWritePreIncImm) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST
             (void)LoadWord(&statestr,instr,LHS + LSImmRHS);
}

static void EMFUNCDECL26(StoreWritePreIncImm) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST
             temp = LHS + LSImmRHS;
             if (StoreWord(&statestr,instr,temp))
                LSBase = temp;
}

static void EMFUNCDECL26(LoadWritePreIncImm) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST
             temp = LHS + LSImmRHS;
             if (LoadWord(&statestr,instr,temp))
                LSBase = temp;
}

static void EMFUNCDECL26(StoreBNoWritePreIncImm) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST
             (void)StoreByte(&statestr,instr,LHS + LSImmRHS);
}

static void EMFUNCDECL26(LoadBNoWritePreIncImm) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST
             (void)LoadByte(&statestr,instr,LHS + LSImmRHS);
}

static void EMFUNCDECL26(StoreBWritePreIncImm) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST
             temp = LHS + LSImmRHS;
             if (StoreByte(&statestr,instr,temp))
                LSBase = temp;
}

static void EMFUNCDECL26(LoadBWritePreIncImm) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST
             temp = LHS + LSImmRHS;
             if (LoadByte(&statestr,instr,temp))
                LSBase = temp;
}


static void EMFUNCDECL26(StoreNoWritePostDecReg) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             if (StoreWord(&statestr,instr,lhs))
                LSBase = lhs - LSRegRHS;
}

static void EMFUNCDECL26(LoadNoWritePostDecReg) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             if (LoadWord(&statestr,instr,lhs))
                LSBase = lhs - LSRegRHS;
}

static void EMFUNCDECL26(StoreWritePostDecReg) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             statestr.NtransSig = LOW;
             if (StoreWord(&statestr,instr,lhs))
                LSBase = lhs - LSRegRHS;
             statestr.NtransSig = (statestr.Mode & 3)?HIGH:LOW;
}

static void EMFUNCDECL26(LoadWritePostDecReg) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             statestr.NtransSig = LOW;
             if (LoadWord(&statestr,instr,lhs))
                LSBase = lhs - LSRegRHS;
             statestr.NtransSig = (statestr.Mode & 3)?HIGH:LOW;
}

static void EMFUNCDECL26(StoreBNoWritePostDecReg) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             if (StoreByte(&statestr,instr,lhs))
                LSBase = lhs - LSRegRHS;
}

static void EMFUNCDECL26(LoadBNoWritePostDecReg) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             if (LoadByte(&statestr,instr,lhs))
                LSBase = lhs - LSRegRHS;
}

static void EMFUNCDECL26(StoreBWritePostDecReg) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             statestr.NtransSig = LOW;
             if (StoreByte(&statestr,instr,lhs))
                LSBase = lhs - LSRegRHS;
             statestr.NtransSig = (statestr.Mode & 3)?HIGH:LOW;
}

static void EMFUNCDECL26(LoadBWritePostDecReg) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             statestr.NtransSig = LOW;
             if (LoadByte(&statestr,instr,lhs))
                LSBase = lhs - LSRegRHS;
             statestr.NtransSig = (statestr.Mode & 3)?HIGH:LOW;
}

static void EMFUNCDECL26(StoreNoWritePostIncReg) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             if (StoreWord(&statestr,instr,lhs))
                LSBase = lhs + LSRegRHS;
}

static void EMFUNCDECL26(LoadNoWritePostIncReg) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             if (LoadWord(&statestr,instr,lhs))
                LSBase = lhs + LSRegRHS;
}

static void EMFUNCDECL26(StoreWritePostIncReg) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             statestr.NtransSig = LOW;
             if (StoreWord(&statestr,instr,lhs))
                LSBase = lhs + LSRegRHS;
             statestr.NtransSig = (statestr.Mode & 3)?HIGH:LOW;
}

static void EMFUNCDECL26(LoadWritePostIncReg) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             statestr.NtransSig = LOW;
             if (LoadWord(&statestr,instr,lhs))
                LSBase = lhs + LSRegRHS;
             statestr.NtransSig = (statestr.Mode & 3)?HIGH:LOW;
}

static void EMFUNCDECL26(StoreBNoWritePostIncReg) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             if (StoreByte(&statestr,instr,lhs))
                LSBase = lhs + LSRegRHS;
}

static void EMFUNCDECL26(LoadBNoWritePostIncReg) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             if (LoadByte(&statestr,instr,lhs))
                LSBase = lhs + LSRegRHS;
}

static void EMFUNCDECL26(StoreBWritePostIncReg) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             statestr.NtransSig = LOW;
             if (StoreByte(&statestr,instr,lhs))
                LSBase = lhs + LSRegRHS;
             statestr.NtransSig = (statestr.Mode & 3)?HIGH:LOW;
}

static void EMFUNCDECL26(LoadBWritePostIncReg) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST
             lhs = LHS;
             statestr.NtransSig = LOW;
             if (LoadByte(&statestr,instr,lhs))
                LSBase = lhs + LSRegRHS;
             statestr.NtransSig = (statestr.Mode & 3)?HIGH:LOW;
}


static void EMFUNCDECL26(StoreNoWritePreDecReg) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST
             (void)StoreWord(&statestr,instr,LHS - LSRegRHS);
}

static void EMFUNCDECL26(LoadNoWritePreDecReg) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST
             (void)LoadWord(&statestr,instr,LHS - LSRegRHS);
}

static void EMFUNCDECL26(StoreWritePreDecReg) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST
             temp = LHS - LSRegRHS;
             if (StoreWord(&statestr,instr,temp))
                LSBase = temp;
}

static void EMFUNCDECL26(LoadWritePreDecReg) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST
             temp = LHS - LSRegRHS;
             if (LoadWord(&statestr,instr,temp))
                LSBase = temp;
}

static void EMFUNCDECL26(StoreBNoWritePreDecReg) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST
             (void)StoreByte(&statestr,instr,LHS - LSRegRHS);
}

static void EMFUNCDECL26(LoadBNoWritePreDecReg) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST
             (void)LoadByte(&statestr,instr,LHS - LSRegRHS);
}

static void EMFUNCDECL26(StoreBWritePreDecReg) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST
             temp = LHS - LSRegRHS;
             if (StoreByte(&statestr,instr,temp))
                LSBase = temp;
}

static void EMFUNCDECL26(LoadBWritePreDecReg) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST
             temp = LHS - LSRegRHS;
             if (LoadByte(&statestr,instr,temp))
                LSBase = temp;
}

static void EMFUNCDECL26(StoreNoWritePreIncReg) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST
             (void)StoreWord(&statestr,instr,LHS + LSRegRHS);
}

static void EMFUNCDECL26(LoadNoWritePreIncReg) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST
             (void)LoadWord(&statestr,instr,LHS + LSRegRHS);
}

static void EMFUNCDECL26(StoreWritePreIncReg) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST
             temp = LHS + LSRegRHS;
             if (StoreWord(&statestr,instr,temp))
                LSBase = temp;
}

static void EMFUNCDECL26(LoadWritePreIncReg) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST
             temp = LHS + LSRegRHS;
             if (LoadWord(&statestr,instr,temp))
                LSBase = temp;
}

static void EMFUNCDECL26(StoreBNoWritePreIncReg) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST
             (void)StoreByte(&statestr,instr,LHS + LSRegRHS);
}

static void EMFUNCDECL26(LoadBNoWritePreIncReg) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST
             (void)LoadByte(&statestr,instr,LHS + LSRegRHS);
}

static void EMFUNCDECL26(StoreBWritePreIncReg) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST
             temp = LHS + LSRegRHS;
             if (StoreByte(&statestr,instr,temp))
                LSBase = temp;
}

static void EMFUNCDECL26(LoadBWritePreIncReg) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST
             temp = LHS + LSRegRHS;
             if (LoadByte(&statestr,instr,temp))
                LSBase = temp;
}

static void EMFUNCDECL26(Undef) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST
  ARMul_UndefInstr(&statestr,instr);
}

static void EMFUNCDECL26(MultiStorePostDec) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST

  STOREMULT(instr,LSBase - LSMNumRegs + 4L,0L);
}

static void EMFUNCDECL26(MultiLoadPostDec) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST

  LOADMULT(instr,LSBase - LSMNumRegs + 4L,0L);
  
}

static void EMFUNCDECL26(MultiStoreWritePostDec) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST

  temp = LSBase - LSMNumRegs;
  STOREMULT(instr,temp + 4L,temp);}

static void EMFUNCDECL26(MultiLoadWritePostDec) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST

  temp = LSBase - LSMNumRegs;
  LOADMULT(instr,temp + 4L,temp);

}

static void EMFUNCDECL26(MultiStoreFlagsPostDec) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST

  STORESMULT(instr,LSBase - LSMNumRegs + 4L,0L);
}

static void EMFUNCDECL26(MultiLoadFlagsPostDec) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST

  LOADSMULT(instr,LSBase - LSMNumRegs + 4L,0L);
}

static void EMFUNCDECL26(MultiStoreWriteFlagsPostDec) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST

  temp = LSBase - LSMNumRegs;
  STORESMULT(instr,temp + 4L,temp);
}

static void EMFUNCDECL26(MultiLoadWriteFlagsPostDec) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST

  temp = LSBase - LSMNumRegs;
  LOADSMULT(instr,temp + 4L,temp);
}

static void EMFUNCDECL26(MultiStorePostInc) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST

  STOREMULT(instr,LSBase,0L);
}

static void EMFUNCDECL26(MultiLoadPostInc) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST

  LOADMULT(instr,LSBase,0L);
}

static void EMFUNCDECL26(MultiStoreWritePostInc) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST

  temp = LSBase;
  STOREMULT(instr,temp,temp + LSMNumRegs);
}

static void EMFUNCDECL26(MultiLoadWritePostInc) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST

  temp = LSBase;
  LOADMULT(instr,temp,temp + LSMNumRegs);
}

static void EMFUNCDECL26(MultiStoreFlagsPostInc) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST

  STORESMULT(instr,LSBase,0L);
}

static void EMFUNCDECL26(MultiLoadFlagsPostInc) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST

  LOADSMULT(instr,LSBase,0L);
}

static void EMFUNCDECL26(MultiStoreWriteFlagsPostInc) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST

  temp = LSBase;
  STORESMULT(instr,temp,temp + LSMNumRegs);
}

static void EMFUNCDECL26(MultiLoadWriteFlagsPostInc) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST

  temp = LSBase;
  LOADSMULT(instr,temp,temp + LSMNumRegs);
}

static void EMFUNCDECL26(MultiStorePreDec) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST

  STOREMULT(instr,LSBase - LSMNumRegs,0L);
}

static void EMFUNCDECL26(MultiLoadPreDec) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST

  LOADMULT(instr,LSBase - LSMNumRegs,0L);
}

static void EMFUNCDECL26(MultiStoreWritePreDec) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST

  temp = LSBase - LSMNumRegs;
  STOREMULT(instr,temp,temp);
}

static void EMFUNCDECL26(MultiLoadWritePreDec) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST

  temp = LSBase - LSMNumRegs;
  LOADMULT(instr,temp,temp);
}

static void EMFUNCDECL26(MultiStoreFlagsPreDec) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST

  STORESMULT(instr,LSBase - LSMNumRegs,0L);
  
}

static void EMFUNCDECL26(MultiLoadFlagsPreDec) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST

  LOADSMULT(instr,LSBase - LSMNumRegs,0L);
}

static void EMFUNCDECL26(MultiStoreWriteFlagsPreDec) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST

  temp = LSBase - LSMNumRegs;
  STORESMULT(instr,temp,temp);
}

static void EMFUNCDECL26(MultiLoadWriteFlagsPreDec) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST

  temp = LSBase - LSMNumRegs;
  LOADSMULT(instr,temp,temp);
}

static void EMFUNCDECL26(MultiStorePreInc) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST

  STOREMULT(instr,LSBase + 4L,0L);
}

static void EMFUNCDECL26(MultiLoadPreInc) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST

  LOADMULT(instr,LSBase + 4L,0L);
}

static void EMFUNCDECL26(MultiStoreWritePreInc) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST

  temp = LSBase;
  STOREMULT(instr,temp + 4L,temp + LSMNumRegs);
}

static void EMFUNCDECL26(MultiLoadWritePreInc) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST

  temp = LSBase;
  LOADMULT(instr,temp + 4L,temp + LSMNumRegs);
}

static void EMFUNCDECL26(MultiStoreFlagsPreInc) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST

  STORESMULT(instr,LSBase + 4L,0L);
}

static void EMFUNCDECL26(MultiLoadFlagsPreInc) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST

  LOADSMULT(instr,LSBase + 4L,0L);
}

static void EMFUNCDECL26(MultiStoreWriteFlagsPreInc) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST

  temp = LSBase;
  STORESMULT(instr,temp + 4L,temp + LSMNumRegs);
}

static void EMFUNCDECL26(MultiLoadWriteFlagsPreInc) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST

  temp = LSBase;
  LOADSMULT(instr,temp + 4L,temp + LSMNumRegs);
}

static void EMFUNCDECL26(CoLoadWritePostDec) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST

  lhs = LHS;
  statestr.Base = lhs - LSCOff;
  ARMul_LDC(&statestr,instr,lhs);
}

static void EMFUNCDECL26(CoStoreNoWritePostDec) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST

  ARMul_STC(&statestr,instr,LHS);

}

static void EMFUNCDECL26(CoLoadNoWritePostDec) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST

  ARMul_LDC(&statestr,instr,LHS);
}

static void EMFUNCDECL26(CoStoreWritePostDec) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST

  lhs = LHS;
  statestr.Base = lhs - LSCOff;
  ARMul_STC(&statestr,instr,lhs);
}

static void EMFUNCDECL26(CoStoreNoWritePostInc) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST

  ARMul_STC(&statestr,instr,LHS);
}

static void EMFUNCDECL26(CoLoadNoWritePostInc) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST

  ARMul_LDC(&statestr,instr,LHS);
}

static void EMFUNCDECL26(CoStoreWritePostInc) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST

  lhs = LHS;
  statestr.Base = lhs + LSCOff;
  ARMul_STC(&statestr,instr,LHS);
}

static void EMFUNCDECL26(CoLoadWritePostInc) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST

  lhs = LHS;
  statestr.Base = lhs + LSCOff;
  ARMul_LDC(&statestr,instr,LHS);
}

static void EMFUNCDECL26(CoStoreNoWritePreDec) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST

  ARMul_STC(&statestr,instr,LHS - LSCOff);
}

static void EMFUNCDECL26(CoLoadNoWritePreDec) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST

  ARMul_LDC(&statestr,instr,LHS - LSCOff);
}

static void EMFUNCDECL26(CoStoreWritePreDec) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST

  lhs = LHS - LSCOff;
  statestr.Base = lhs;
  ARMul_STC(&statestr,instr,lhs);
}

static void EMFUNCDECL26(CoLoadWritePreDec) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST

  lhs = LHS - LSCOff;
  statestr.Base = lhs;
  ARMul_LDC(&statestr,instr,lhs);
}

static void EMFUNCDECL26(CoStoreNoWritePreInc) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST

  ARMul_STC(&statestr,instr,LHS + LSCOff);

}

static void EMFUNCDECL26(CoLoadNoWritePreInc) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST

  ARMul_LDC(&statestr,instr,LHS + LSCOff);
}

static void EMFUNCDECL26(CoStoreWritePreInc) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST

  lhs = LHS + LSCOff;
  statestr.Base = lhs;
  ARMul_STC(&statestr,instr,lhs);
}

static void EMFUNCDECL26(CoLoadWritePreInc) (ARMword instr, ARMword pc) {
  register ARMword lhs;
  EMFUNC_CONDTEST

  lhs = LHS + LSCOff;
  statestr.Base = lhs;
  ARMul_LDC(&statestr,instr,lhs);
}

static void EMFUNCDECL26(CoMCRDataOp) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST
             if (BIT(4)) { /* MCR */
                if (DESTReg == 15) {
                   ARMul_MCR(&statestr,instr,ECC | ER15INT | EMODE |
                                          ((statestr.Reg[15] + 4) & R15PCBITS) );
                   }
                else
                   ARMul_MCR(&statestr,instr,DEST);
                }
             else /* CDP Part 1 */
                ARMul_CDP(&statestr,instr);
}

static void EMFUNCDECL26(CoMRCDataOp) (ARMword instr, ARMword pc) {
  register ARMword temp;
  EMFUNC_CONDTEST

             if (BIT(4)) { /* MRC */
                temp = ARMul_MRC(&statestr,instr);
                if (DESTReg == 15) {
                   ASSIGNN((temp & NBIT) != 0);
                   ASSIGNZ((temp & ZBIT) != 0);
                   ASSIGNC((temp & CBIT) != 0);
                   ASSIGNV((temp & VBIT) != 0);
                   }
                else
                   DEST = temp;
                }
             else /* CDP Part 2 */
                ARMul_CDP(&statestr,instr);
}

static void EMFUNCDECL26(SWI) (ARMword instr, ARMword pc) {
  EMFUNC_CONDTEST
             if (instr == ARMul_ABORTWORD && statestr.AbortAddr == pc) { /* a prefetch abort */
                ARMul_Abort(&statestr,ARMul_PrefetchAbortV);
                return;
                }

                ARMul_Abort(&statestr,ARMul_SWIV);
}

static void EMFUNCDECL26(Noop) (ARMword instr, ARMword pc) {
}
