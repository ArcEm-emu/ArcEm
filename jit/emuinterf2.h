#ifndef JITEMUINTERF2_HEADER
#define JITEMUINTERF2_HEADER

#include <stddef.h>
#include "codegen.h"
#include "jit.h"
#include "../armdefs.h"
#include "regalloc.h" /* YUCK */
#include "decoder.h"

/* Extra emulator interfaces */

static inline uint32_t JITEmuInterf2_LoadReg(uint32_t cc, uint32_t hostreg, uint32_t statereg, uint32_t emureg)
{
  return JITCodeGen_LoadImm(cc,hostreg,statereg,offsetof(ARMul_State,Reg[emureg]));
}

static inline uint32_t JITEmuInterf2_StoreReg(uint32_t cc, uint32_t hostreg, uint32_t statereg, uint32_t emureg)
{
  return JITCodeGen_StoreImm(cc,hostreg,statereg,offsetof(ARMul_State,Reg[emureg]));
}

static inline uint32_t JITEmuInterf2_LoadCycleCount(uint32_t cc, uint32_t hostreg, uint32_t statereg)
{
  return JITCodeGen_LoadImm(cc,hostreg,statereg,offsetof(ARMul_State,NumCycles));
}

static inline uint32_t JITEmuInterf2_StoreCycleCount(uint32_t cc, uint32_t hostreg, uint32_t statereg)
{
  return JITCodeGen_StoreImm(cc,hostreg,statereg,offsetof(ARMul_State,NumCycles));
}

extern void JITEmuInterf2_WriteEpilogue(JITRegAlloc *ra, JITResult result);

extern void JITEmuInterf2_WriteLoad(JITRegAlloc *ra, JITHostReg Rd, JITHostReg Raddr, bool byte, uintptr_t abort);

extern void JITEmuInterf2_WriteLDM(JITRegAlloc *ra, JITHostReg Rn, JITHostReg Raddr, const Instruction *instr, uintptr_t abort);

#endif
