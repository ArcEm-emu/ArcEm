#include "emuinterf.h"
#include "../arch/armarc.h"
#include "emuinterf2.h"
#include "decoder.h"
#include "codeblocks.h"

JITEmuState *JITEmuInterf_GetState(void)
{
  return &statestr;
}

void JITEmuInterf2_WriteEpilogue(JITRegAlloc *ra, JITResult result)
{
#ifdef DEBUG_JIT_FORCE_NORMAL
  result = JITResult_Normal;
#endif
  JITHostReg tempreg = JITRegAlloc_MapReg(ra, JIT_E_Temp, 1, JIT_H_REG_NONE, JIT_NV);
  /* Set correct pipeline state
     The interpreter assumes NORMAL when executing instructions, so if we're returning with JITResult_Interpret then we must be NORMAL
     Otherwise, set to PCINCED since we have fully advanced the PC */
  ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rd_Imm(JIT_AL,JIT_MOV,tempreg,(result==JITResult_Normal?PCINCED:NORMAL)));
  ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_StoreImm(JIT_AL,tempreg,ra->emu_regs[JIT_E_StateReg].hostreg,offsetof(ARMul_State,NextInstr)));
  /* XXX Aborted, AbortSig? */
  /* Return */
  JITRegAlloc_LockReg(ra,tempreg,-1,false);
  JITRegAlloc_WriteBackDirty(ra);
  /* XXX hack, using R0 without claiming */
  ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rd_Imm(JIT_AL,JIT_MOV,0,result));
  JITRegAlloc_CalleeRestore(ra, true);
}

void JITEmuInterf2_WriteLoad(JITRegAlloc *ra, JITHostReg Rd, JITHostReg Raddr, bool byte, uintptr_t abort)
{
  JITHostReg fastmap, fastmapmode, temp;
  /* Check for address exceptions */
  ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rn_Imm(JIT_AL, JIT_CMP, Raddr, 0x04000000)); /* CMP to clear V */
  /* Load the fastmap regs */
  fastmap = JITRegAlloc_MapReg(ra, JIT_E_Temp2, 1, JIT_H_REG_NONE, JIT_NV);
  fastmapmode = JITRegAlloc_MapReg(ra, JIT_E_Temp3, 1, JIT_H_REG_NONE, JIT_NV);
  temp = JITRegAlloc_MapReg(ra, JIT_E_Temp4, 1, JIT_H_REG_NONE, JIT_NV);
  /* Now that regs are allocated, we can safely branch away */
  ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_LoadImm(JIT_AL,fastmap,ra->emu_regs[JIT_E_StateReg].hostreg,offsetof(ARMul_State,FastMap)));
  ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_LoadImm(JIT_AL,fastmapmode,ra->emu_regs[JIT_E_StateReg].hostreg,offsetof(ARMul_State,FastMapMode)));
  /* Trigger address exception */
  ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_Branch(JIT_CS, abort-ra->block->nextinstr));
  /* Load FlagsAndData */
  ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rd_Rn_Imm(JIT_AL, JIT_BIC, temp, Raddr, 0xf00));
  ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_Load_Rm(JIT_AL,temp,fastmap,temp | 0x4a0)); /* LSR #9 */
  /* Check type & flags */
  ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rn_Rm(JIT_AL, JIT_TST, temp, fastmapmode));
  /* Reduced decoding: normally if all of bits 24-30 are clear it signifies an abort, with bit 31 signifying whether an access function is required
     But we only deal with the direct access case, so if N is set or 24-30 are clear we need to bail
     V=0 from CMP, so we can use LE condition code to detect failure */
  ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_Branch(JIT_LE, abort-ra->block->nextinstr));
  /* Now we can load the data! */
  if (byte) {
    ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_Load_Rm(JIT_AL | (1<<22),Rd,Raddr,temp | 0x400)); /* LSL #8 */
  } else {
    /* TODO do rotated load if host supports it */
    ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rd_Rn_Imm(JIT_AL, JIT_BIC, fastmapmode, Raddr, 3));
    ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rd_Rm(JIT_AL, JIT_MOV | JIT_S, fastmap, Raddr | 0xf80)); /* LSL #31 */
    ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_Load_Rm(JIT_AL,Rd,fastmapmode,temp | 0x400)); /* LSL #8 */
    ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rd_Rm(JIT_CS, JIT_MOV, Rd, Rd | 0x860)); /* ROR #16 */    
    ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rd_Rm(JIT_MI, JIT_MOV, Rd, Rd | 0x460)); /* ROR #8 */
  }
  /* Release regs */
  JITRegAlloc_LockReg(ra, fastmap, -1, false);
  JITRegAlloc_LockReg(ra, fastmapmode, -1, false);
  JITRegAlloc_LockReg(ra, temp, -1, false);
}

void JITEmuInterf2_WriteLDM(JITRegAlloc *ra, JITHostReg Rn, JITHostReg Raddr, const Instruction *instr, uintptr_t abort)
{
  JITHostReg fastmap, fastmapmode, temp;
  int i = Decoder_LDMSTM_NumRegs(instr);
  /* Load the fastmap regs */
  fastmap = JITRegAlloc_MapReg(ra, JIT_E_Temp2, 1, JIT_H_REG_NONE, JIT_NV);
  fastmapmode = JITRegAlloc_MapReg(ra, JIT_E_Temp3, 1, JIT_H_REG_NONE, JIT_NV);
  temp = JITRegAlloc_MapReg(ra, JIT_E_Temp4, 1, JIT_H_REG_NONE, JIT_NV);
  /* Check for crossing page boundaries */
  if (i > 1) {
    ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rd_Rm(JIT_AL, JIT_MOV, temp, Raddr | 0xa00)); /* LSL #20 */
    ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rd_Rn_Imm(JIT_AL, JIT_ADD | JIT_S, temp, temp, (i-1)<<(2+20)));
  }
  ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_LoadImm(JIT_AL,fastmap,ra->emu_regs[JIT_E_StateReg].hostreg,offsetof(ARMul_State,FastMap)));
  ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_LoadImm(JIT_AL,fastmapmode,ra->emu_regs[JIT_E_StateReg].hostreg,offsetof(ARMul_State,FastMapMode)));
  /* Check for address exceptions */
  ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rn_Imm((i > 1 ? JIT_CC : JIT_AL), JIT_CMP, Raddr, 0x04000000)); /* CMP to clear V */
  /* Trigger address exception */
  ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_Branch(JIT_CS, abort-ra->block->nextinstr));
  /* Load FlagsAndData */
  ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rd_Rn_Imm(JIT_AL, JIT_BIC, temp, Raddr, 0xf00));
  ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_Load_Rm(JIT_AL,temp,fastmap,temp | 0x4a0)); /* LSR #9 */
  /* Check type & flags */
  ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rn_Rm(JIT_AL, JIT_TST, temp, fastmapmode));
  /* Reduced decoding: normally if all of bits 24-30 are clear it signifies an abort, with bit 31 signifying whether an access function is required
     But we only deal with the direct access case, so if N is set or 24-30 are clear we need to bail
     V=0 from CMP, so we can use LE condition code to detect failure */
  ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_Branch(JIT_LE, abort-ra->block->nextinstr));
  /* Update Raddr to be the actual address */
  ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rd_Rn_Rm(JIT_AL, JIT_ADD, Raddr, Raddr, temp | 0x400)); /* LSL #8 */
  /* Load into the register if it's resident, otherwise go via temp */
  for(i=0;i<16;i++) {
    if (!(instr->instr & (1<<i))) {
      continue;
    }
    JITHostReg dest = ra->emu_regs[i].hostreg;
    if (dest == JIT_H_REG_NONE) {
      dest = temp;
    }
    ForwardCodeBlock_WriteCode(ra->block, 0xe4900004 | (dest << 12) | (Raddr << 16)); /* LDR dest, [Raddr], #4 */
    /* Immediately write back the value (yuck) */
    ForwardCodeBlock_WriteCode(ra->block, JITEmuInterf2_StoreReg(JIT_AL, dest, ra->emu_regs[JIT_E_StateReg].hostreg, i));
  }
  /* Release regs */
  JITRegAlloc_LockReg(ra, fastmap, -1, false);
  JITRegAlloc_LockReg(ra, fastmapmode, -1, false);
  JITRegAlloc_LockReg(ra, temp, -1, false);
}
