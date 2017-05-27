#include <limits.h>
#include <assert.h>
#include <string.h>
#include "regalloc.h"
#include "emuinterf2.h"
#include "decoder.h"

void JITRegAlloc_Init(JITRegAlloc *ra, ForwardCodeBlock *block)
{
  int i;
  memset(ra, 0, sizeof(JITRegAlloc));
  for (i=0;i<JIT_H_REG_NUM;i++)
  {
    ra->host_regs[i].emureg = JIT_E_REG_NONE;
  }
  for (i=0;i<JIT_E_REG_NUM;i++)
  {
    ra->emu_regs[i].hostreg = JIT_H_REG_NONE;
  }
  ra->free_host_regs = (1<<12) + 0xe; /* initially used for state ptr */
  ra->callee_save = 0x4ff0; /* r4-r11, r14 */
  ra->block = block;
  /* Init the state reg */
  ra->emu_regs[JIT_E_StateReg].hostreg = JIT_H_R0;
  ra->host_regs[JIT_H_R0].emureg = JIT_E_StateReg;
  ra->host_regs[JIT_H_R0].required = true;
}

void JITRegAlloc_Copy(JITRegAlloc *dest, JITRegAlloc *src, ForwardCodeBlock *block)
{
  /* Forget any callee-save instr, for both src and dest
     This prevents the two going out-of-sync if either src or dest need to allocate new callee-save regs */
  src->callee_save_instr = NULL;
  memcpy(dest, src, sizeof(JITRegAlloc));
  dest->block = block;
}

void JITRegAlloc_Fork(JITRegAlloc *parent, JITRegAlloc *child)
{
  /* Force write-back of any dirty regs (we can't guarantee they'll still be mapped when we join) */
  JITRegAlloc_WriteBackDirty(parent);
  /* Unlike Copy, we can retain the callee-save instr */
  memcpy(child, parent, sizeof(JITRegAlloc));
}

void JITRegAlloc_Join(JITRegAlloc *parent, JITRegAlloc *child)
{
  int i;
  /* Now resolve the register mappings; if host reg has same emu reg for both paths, merge dirty flags
     Else force any writeback */
  for(i=0;i<JIT_H_REG_NUM;i++) {
    if (parent->host_regs[i].emureg == child->host_regs[i].emureg) {
      parent->host_regs[i].dirty |= child->host_regs[i].dirty;
    } else {
      JITRegAlloc_FlushHostReg(child, (JITHostReg) i);
      JITRegAlloc_FlushHostReg(parent, (JITHostReg) i); /* Shouldn't trigger a store */
    }
  }
  if (parent->callee_save_instr)
  {
    /* Pull apart this instruction to see if there are any saved registers that were added by the child (it's safest for us to work it out this way, rather than examine the child) */
    uint32_t saved = 0;
    if (((*parent->callee_save_instr) & 0x0f000000) == 0x05000000) {
      saved = (1<<(((*parent->callee_save_instr) >> 12) & 15));
    } else {
      saved = (*parent->callee_save_instr) & 0xffff;
    }
    saved &= parent->callee_save;
    parent->callee_save &= ~saved;
    parent->callee_saved |= saved;
    parent->free_host_regs |= saved;
  }
  /* If there are any registers the child pushed which we can't locate the save instr for, get the child to restore them itself */
  child->callee_saved &= ~parent->callee_saved;
  JITRegAlloc_CalleeRestore(child, false);
  /* Update block reference */
  parent->block = child->block;
  /* We must forget any callee-save instr, since any update we make to it will put it out of sync with any restore the child performed
     TODO - Have a flag in JITRegAlloc for whether the child has restored? */
  parent->callee_save_instr = NULL;
}

void JITRegAlloc_CalleeSave(JITRegAlloc *ra, JITHostReg hr)
{
  /* XXX should be in EmuInterf */
  ra->callee_saved |= (1<<hr);
  if (!ra->callee_save_instr) {
    ra->callee_save_instr = (uint32_t *) ForwardCodeBlock_WriteCode(ra->block, 0xe52d0004 | (hr<<12)); /* STR Rn,[R13,#-4]! */
  } else {
    if (((*ra->callee_save_instr) & 0x0f000000) == 0x05000000) { /* STR? */
      *ra->callee_save_instr = 0xe92d0000 | (1<<(((*ra->callee_save_instr) >> 12) & 15)); /* Convert to STM */
    }
    *ra->callee_save_instr |= (1<<hr); /* n.b. can't use callee_saved since we might be chaining STR/STM */
  }
  /* Update register lists */
  ra->free_host_regs |= 1<<hr;
  ra->callee_save &= ~(1<<hr);
}

JITHostReg JITRegAlloc_MapReg(JITRegAlloc *ra, JITEmuReg r, int required, JITHostReg hostreg, uint32_t cc)
{
  if (hostreg == JIT_H_REG_NONE) {
    if (ra->emu_regs[r].hostreg == JIT_H_REG_NONE) {
      hostreg = JITRegAlloc_GetFreeHostReg(ra);
    } else {
      hostreg = ra->emu_regs[r].hostreg;
      ra->host_regs[hostreg].required += required;
      return hostreg; /* already resident */
    }
  }
  if (ra->emu_regs[r].hostreg == hostreg) {
    ra->host_regs[hostreg].required += required;
    return hostreg;
  }
  JITRegAlloc_FlushHostReg(ra, hostreg);
  if (ra->emu_regs[r].hostreg != JIT_H_REG_NONE) {
    if (cc != JIT_NV)
    {
      ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rd_Rm(cc, JIT_MOV, hostreg, ra->emu_regs[r].hostreg)); /* needs to deal with move to/from PSR? */
    }
    ra->host_regs[hostreg] = ra->host_regs[ra->emu_regs[r].hostreg];
    ra->free_host_regs |= 1 << ra->emu_regs[r].hostreg;
    ra->host_regs[ra->emu_regs[r].hostreg] = (JITHostRegState) { JIT_E_REG_NONE, false, 0 };
  } else if (cc != JIT_NV) {
    /* XXX make general */
    if (r < JIT_E_CycleCount) {
      ForwardCodeBlock_WriteCode(ra->block, JITEmuInterf2_LoadReg(cc, hostreg, ra->emu_regs[JIT_E_StateReg].hostreg, r));
    } else if (r == JIT_E_CycleCount) {
      ForwardCodeBlock_WriteCode(ra->block, JITEmuInterf2_LoadCycleCount(cc, hostreg, ra->emu_regs[JIT_E_StateReg].hostreg));
    }
#ifdef DEBUG_JIT_METRICS_EXEC
    else if (r == JIT_E_Exec) {
      ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_LoadImm(cc,hostreg,ra->emu_regs[JIT_E_StateReg].hostreg,offsetof(ARMul_State,jit)+offsetof(JITState,exec_count)));
    }
#endif
  }
  ra->emu_regs[r].hostreg = hostreg;
  ra->host_regs[hostreg].emureg = r;
  ra->host_regs[hostreg].required += required;
  ra->free_host_regs &= ~(1<<hostreg);
  return hostreg;
}

void JITRegAlloc_UnmapReg(JITRegAlloc *ra, JITEmuReg r)
{
  if (r == JIT_E_REG_NONE) {
    return;
  }
  JITHostReg hr = ra->emu_regs[r].hostreg;
  if (hr == JIT_H_REG_NONE) {
    return;
  }
  assert(!ra->host_regs[hr].required);
  JITRegAlloc_WriteBack(ra, hr);
  ra->free_host_regs |= 1 << hr;
  ra->host_regs[ra->emu_regs[r].hostreg] = (JITHostRegState) { JIT_E_REG_NONE, false, 0 };
  ra->emu_regs[r] = (JITEmuRegState) { JIT_H_REG_NONE };
}

JITHostReg JITRegAlloc_GetFreeHostReg(JITRegAlloc *ra)
{
  int start = ra->next_idx;
  /* weight the registers by the associated cost:
     1. any from free_host_regs
     2. any where !required && !dirty
        -> later on, might want to associate an explicit cost with reloading/recalculating values?
     3. highest numbered reg from callee_save
        (must be highest numbered so we can have multiple STR/STM but only one LDR/LDM)
        -> later on, might want to give these explicit costs as well? ldm/stm can transfer quickly, need to be able to represent if the next register to claim is going to be quick or slow
     4. any where !required && dirty
        -> later on, might want to associate an explicit cost; if the register is no longer needed then writing it back will be cheaper than taking from the callee-save list
     could do this in one pass, tracking the cost associated with using a given register
  */
  int best_cost = INT_MAX;
  int best_idx = 0;
#define CANDIDATE(cost) if (best_cost > cost) { best_idx = ra->next_idx; best_cost = cost; }
  do {
    ra->next_idx = (ra->next_idx + 1) & 15;
    if (ra->free_host_regs & (1<<ra->next_idx)) {
      CANDIDATE(1) /* rule 1 */
      break; /* always going to win, so stop here */
    } else if (ra->callee_save & (1<<ra->next_idx)) {
      CANDIDATE(3 + 16 - ra->next_idx) /* rule 3 */
    } else if ((ra->host_regs[ra->next_idx].emureg != JIT_E_REG_NONE) && !ra->host_regs[ra->next_idx].required) {
      if (ra->host_regs[ra->next_idx].dirty) {
        CANDIDATE(4 + 20) /* rule 4 */
      } else {
        CANDIDATE(2)
        if (!ra->free_host_regs) {
          break; /* not going to get any better than this */
        }
      }
    }
  } while(start != ra->next_idx);
  assert(best_cost != INT_MAX);
  ra->next_idx = best_idx;
  if (ra->callee_save & (1<<best_idx)) {
    /* preserve contents of callee-save register */
    JITRegAlloc_CalleeSave(ra, best_idx); /* assume this will track which registers need restoring, and will update ra->callee_save & ra->free_host_regs to indicate which register(s) have become available */
  } else if (!(ra->free_host_regs & (1<<best_idx))) {
    /* reclaim register that's already in use */
    JITRegAlloc_FlushHostReg(ra, best_idx);
  } 
  return (JITHostReg) best_idx;
}

void JITRegAlloc_UnmapAll(JITRegAlloc *ra)
{
  int i;
  for (i=0; i<JIT_H_REG_NUM; i++)
  {
    JITRegAlloc_FlushHostReg(ra, i);
  }
}

void JITRegAlloc_WriteBack(JITRegAlloc *ra, JITHostReg hr)
{
  JITEmuReg r;
  if ((hr == JIT_H_REG_NONE) || (!ra->host_regs[hr].dirty)) {
    return;
  }
  /* XXX make general */
  r = ra->host_regs[hr].emureg;
  if (r < JIT_E_CycleCount) {
    ForwardCodeBlock_WriteCode(ra->block, JITEmuInterf2_StoreReg(JIT_AL, hr, ra->emu_regs[JIT_E_StateReg].hostreg, r));
  } else if (r == JIT_E_CycleCount) {
    ForwardCodeBlock_WriteCode(ra->block, JITEmuInterf2_StoreCycleCount(JIT_AL, hr, ra->emu_regs[JIT_E_StateReg].hostreg));
  }
#ifdef DEBUG_JIT_METRICS_EXEC
  else if (r == JIT_E_Exec) {
    ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_StoreImm(JIT_AL,hr,ra->emu_regs[JIT_E_StateReg].hostreg,offsetof(ARMul_State,jit)+offsetof(JITState,exec_count)));
  }
#endif
  ra->host_regs[hr].dirty = false;
}

void JITRegAlloc_WriteBackUnrequired(JITRegAlloc *ra)
{
  int i;
  for (i=0; i<JIT_H_REG_NUM; i++)
  {
    if (!ra->host_regs[i].required)
    {
      JITRegAlloc_WriteBack(ra, i);
    }
  }
}

void JITRegAlloc_WriteBackDirty(JITRegAlloc *ra)
{
  int i;
  for (i=0; i<JIT_H_REG_NUM; i++)
  {
    if (ra->host_regs[i].dirty)
    {
      JITRegAlloc_WriteBack(ra, i);
    }
  }
}

void JITRegAlloc_CalleeRestore(JITRegAlloc *ra, bool ret)
{
  /* Write back everything */
  JITRegAlloc_WriteBackDirty(ra);
  /* Restore callee-save regs and/or return */
  if (ra->callee_saved)
  {
    if (ra->callee_saved == (1<<14))
    {
      ForwardCodeBlock_WriteCode(ra->block, (ret ? 0xe49df004 : 0xe49de004));  /* LDR PC or R14 */
    }
    else
    {
      uint32_t ldm = 0xe8bd0000 | ra->callee_saved;
      if (ret)
      {
        ldm ^= 3<<14;
      }
      ForwardCodeBlock_WriteCode(ra->block,ldm); 
    }
  }
  else if (ret)
  {
    ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rd_Rm(JIT_AL,JIT_MOV,15,14));
  }
  /* Reset our state */
  JITRegAlloc_Init(ra, ra->block);
}
