#include <assert.h>
#include <stdlib.h>
#include "jit.h"
#include "jitpage.h"
#include "memattr.h"
#include "decoder.h"
#include "codeblocks.h"
#include "emuinterf2.h"
#include "metrics.h"
#include "regalloc.h"

#ifdef __riscos__
#include <kernel.h>
#include <swis.h>
#endif

void JIT_Init(JITState *jit,uintptr_t romramchunk,uint32_t romramchunksize)
{
  int i;
  int count = romramchunksize/4;
  memset(jit,0,sizeof(JITState));
  jit->romramchunk = romramchunk;
  jit->phy2func = (JITFunc *) malloc(sizeof(JITFunc)*count);
  for (i=0;i<count;i++)
  {
    jit->phy2func[i] = &JIT_Generate;
  }
  jit->addr2func = ((uintptr_t) jit->phy2func)-romramchunk;
  JITPage_Init(jit,romramchunksize);
  MemAttr_Init(jit,romramchunksize);
}

static inline bool JITable(const Instruction *instr)
{
  switch (instr->type)
  {
  case InstrType_NOP:
    return true;
  case InstrType_DataProc:
    /* Don't allow R15 as dest */
    if (Decoder_DataProc_UsesRd(instr) && (Decoder_Rd(instr) == 15)) {
      return false;
    }
    return true;
#ifndef DEBUG_JIT_TEST_EXEC
  case InstrType_Branch:
    return true;
#endif
  case InstrType_LDRSTR:
    /* Only load, non-T instructions which don't write R15 */
    if (Decoder_LDRSTR_WritebackFlag(instr) && (Decoder_Rn(instr) == 15)) {
      return false;
    }
    return (Decoder_LDRSTR_LoadFlag(instr) && (Decoder_Rd(instr) != 15) && !Decoder_LDRSTR_TFlag(instr));
  case InstrType_LDMSTM:
    /* Only load, non-hat, non-PC using */
    if (Decoder_LDMSTM_HatFlag(instr) || (Decoder_Rn(instr) == 15) || (instr->instr & 0x8000) || !Decoder_LDMSTM_LoadFlag(instr)) {
      return false;
    }
    return true;
  default:
    return false;
  }
}

typedef struct {
  bool psr_loaded;
  bool psr_dirty;
  int cycles;
  int instrs;
} CodeGenState;

static void codegen_sync(JITRegAlloc *ra,CodeGenState *cgstate)
{
  if (cgstate->instrs || cgstate->cycles || cgstate->psr_dirty)
  {
    JITHostReg cyc = JITRegAlloc_LockEmuReg(ra, JIT_E_CycleCount);
    JITHostReg pc = JITRegAlloc_LockEmuReg(ra, JIT_E_PC);
    JITHostReg temp = (cgstate->psr_dirty ? JITRegAlloc_LockEmuReg(ra, JIT_E_Temp) : JIT_H_REG_NONE);
#ifdef DEBUG_JIT_METRICS_EXEC
    JITHostReg exec = JITRegAlloc_LockEmuReg(ra, JIT_E_Exec);
#endif
    JITRegAlloc_WriteBackUnrequired(ra); /* Free up some regs */
    if (cyc == JIT_H_REG_NONE) {
      cyc = JITRegAlloc_MapReg(ra, JIT_E_CycleCount, 1, JIT_H_REG_NONE, JIT_AL);
    }
    if (pc == JIT_H_REG_NONE) {
      pc = JITRegAlloc_MapReg(ra, JIT_E_PC, 1, JIT_H_REG_NONE, JIT_AL);
    }
    if (cgstate->psr_dirty && (temp == JIT_H_REG_NONE)) {
      temp = JITRegAlloc_MapReg(ra, JIT_E_Temp, 1, JIT_H_REG_NONE, JIT_NV);
    }
#ifdef DEBUG_JIT_METRICS_EXEC
    if (exec == JIT_H_REG_NONE) {
      exec = JITRegAlloc_MapReg(ra, JIT_E_Exec, 1, JIT_H_REG_NONE, JIT_AL);
    }
    ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rd_Rn_Imm(JIT_AL,JIT_ADD,exec,exec,cgstate->instrs));
    JITRegAlloc_LockReg(ra, exec, -1, true);
#endif
    if (cgstate->psr_dirty) {
      ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_SavePSR(JIT_AL,temp));
    }
    ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rd_Rn_Imm(JIT_AL,JIT_ADD,cyc,cyc,cgstate->cycles));
    ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rd_Rn_Imm(JIT_AL,JIT_ADD,pc,pc,cgstate->instrs<<2));
    if (cgstate->psr_dirty) { /* TODO prime candidate for BFI */
      ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rd_Rn_Imm(JIT_AL,JIT_AND,temp,temp,0xF0000000));
      ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rd_Rn_Imm(JIT_AL,JIT_BIC,pc,pc,0xF0000000));
      ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rd_Rn_Rm(JIT_AL,JIT_ORR,pc,pc,temp));
    }
    JITRegAlloc_LockReg(ra, cyc, -1, true);
    JITRegAlloc_LockReg(ra, pc, -1, true);
    JITRegAlloc_LockReg(ra, temp, -1, false);
    cgstate->psr_loaded = cgstate->psr_dirty = false;
    cgstate->cycles = cgstate->instrs = 0;
  }
}

static void codegen_sync_pc(JITRegAlloc *ra,CodeGenState *cgstate,bool psr)
{
  psr &= cgstate->psr_dirty;
  if (cgstate->instrs || psr)
  {
    JITHostReg pc = JITRegAlloc_LockEmuReg(ra, JIT_E_PC);
    JITHostReg temp = (psr ? JITRegAlloc_LockEmuReg(ra, JIT_E_Temp) : JIT_H_REG_NONE);
#ifdef DEBUG_JIT_METRICS_EXEC
    JITHostReg exec = JITRegAlloc_LockEmuReg(ra, JIT_E_Exec);
#endif
    if (pc == JIT_H_REG_NONE) {
      pc = JITRegAlloc_MapReg(ra, JIT_E_PC, 1, JIT_H_REG_NONE, JIT_AL);
    }
    if (psr && (temp == JIT_H_REG_NONE)) {
      temp = JITRegAlloc_MapReg(ra, JIT_E_Temp, 1, JIT_H_REG_NONE, JIT_NV);
    }
#ifdef DEBUG_JIT_METRICS_EXEC
    if (exec == JIT_H_REG_NONE) {
      exec = JITRegAlloc_MapReg(ra, JIT_E_Exec, 1, JIT_H_REG_NONE, JIT_AL);
    }
    ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rd_Rn_Imm(JIT_AL,JIT_ADD,exec,exec,cgstate->instrs));
    JITRegAlloc_LockReg(ra, exec, -1, true);
#endif
    if (psr) {
      ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_SavePSR(JIT_AL,temp));
    }
    if (cgstate->instrs) {
      ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rd_Rn_Imm(JIT_AL,JIT_ADD,pc,pc,cgstate->instrs<<2));
    }
    if (psr) { /* TODO prime candidate for BFI */
      ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rd_Rn_Imm(JIT_AL,JIT_AND,temp,temp,0xF0000000));
      ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rd_Rn_Imm(JIT_AL,JIT_BIC,pc,pc,0xF0000000));
      ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rd_Rn_Rm(JIT_AL,JIT_ORR,pc,pc,temp));
      cgstate->psr_dirty = false;
    }
    JITRegAlloc_LockReg(ra, pc, -1, true);
    JITRegAlloc_LockReg(ra, temp, -1, false);
    cgstate->instrs = 0;
  }
}

static void codegen_conditional_cycles(JITRegAlloc *ra,CodeGenState *cgstate,uint32_t cc,int count)
{
  if (cc == JIT_NV)
  {
    return;
  }
  if (cc == JIT_AL)
  {
    cgstate->cycles += count;
  }
  else if (count)
  {
    JITHostReg cyc = JITRegAlloc_MapReg(ra, JIT_E_CycleCount, 1, JIT_H_REG_NONE, JIT_AL);
    ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rd_Rn_Imm(cc,JIT_ADD,cyc,cyc,cgstate->cycles));
    JITRegAlloc_LockReg(ra, cyc, -1, true);    
  }
}

static void codegen_bigadd(ForwardCodeBlock *block, JITHostReg Rd, JITHostReg Rn, int val)
{
  int op = JIT_ADD;
  int shift = 0;
  if (val < 0) {
    op = JIT_SUB;
    val = -val;
  } else if (!val) {
    if (Rd != Rn) {
      ForwardCodeBlock_WriteCode(block, JITCodeGen_DataProc_Rd_Rm(JIT_AL, JIT_MOV, Rd, Rn));
    }
    return;
  }
  while (val) {
    /* XX CLZ */
    while (!((val >> shift) & 0x3)) {
      shift += 2;
    }
    ForwardCodeBlock_WriteCode(block, JITCodeGen_DataProc_Rd_Rn_Imm(JIT_AL,op,Rd,Rn,val & (0xff<<shift)));
    Rn = Rd;
    val &= ~(0xff<<shift);
    shift += 8;
  }
}

static void codegen_handle_branch(JITRegAlloc *ra, CodeGenState *cgstate, const Instruction *instr)
{
  /* Prep PC */
  JITHostReg pc = JITRegAlloc_MapReg(ra, JIT_E_PC, 1, JIT_H_REG_NONE, JIT_AL);
  cgstate->cycles += 2;
  codegen_sync(ra,cgstate);
  if (Decoder_Branch_BLFlag(instr)) {
    /* Copy PC to R14 */
    JITHostReg r14 = JITRegAlloc_MapReg(ra, JIT_ER(14), 1, JIT_H_REG_NONE, JIT_NV);
    ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rd_Rn_Imm(JIT_AL,JIT_SUB,r14,pc,8));
    JITRegAlloc_LockReg(ra, r14, -1, true);
  }
  /* Add branch offset */
  int32_t offset = Decoder_Branch_Offset(instr)-4;
  if (offset) {
    /* To avoid needing a temp reg to deal with overflow, just rotate the PC + offset */
    /* XXX temp reg could be better if e.g. PSR is already loaded */
    offset <<= 6;
    ForwardCodeBlock_WriteCode(ra->block, 0xe1a00d60 + (pc*0x1001)); /* MOV pc,pc,ROR #26 */
    codegen_bigadd(ra->block, pc, pc, offset);
    ForwardCodeBlock_WriteCode(ra->block, 0xe1a00360 + (pc*0x1001)); /* MOV pc,pc,ROR #6 */
  }
  JITRegAlloc_LockReg(ra, pc, -1, true);
  /* Now exit */
  JITEmuInterf2_WriteEpilogue(ra,JITResult_Normal);
}

static void codegen_handle_branch_cc(JITRegAlloc *ra1, const CodeGenState *cgstate1, ForwardCodeBlock *block, const Instruction *instr)
{
  JITRegAlloc ra2;
  CodeGenState cgstate2;
  memcpy(&cgstate2,cgstate1,sizeof(CodeGenState));
  JITRegAlloc_Copy(&ra2,ra1,block);
  codegen_handle_branch(&ra2,&cgstate2,instr);
}

static void codegen_handle_ldr(JITRegAlloc *ra,const CodeGenState *cgstate,const Instruction *instr, ForwardCodeBlock *block2)
{
  JITHostReg Rn, Rd, Rm = JIT_H_REG_NONE, Raddr, Ralu, Roffset;

  /* Pre-lock registers */
  Rn = JITRegAlloc_LockEmuReg(ra, Decoder_Rn(instr));
  Rd = JITRegAlloc_LockEmuReg(ra, Decoder_Rd(instr));

  /* Map registers */
  if (!Decoder_LDRSTR_ImmFlag(instr)) {
    Rm = JITRegAlloc_MapReg(ra, Decoder_Rm(instr), 1, JIT_H_REG_NONE, JIT_AL);
  }
  if (Rn == JIT_H_REG_NONE) {
    Rn = JITRegAlloc_MapReg(ra, Decoder_Rn(instr), 1, JIT_H_REG_NONE, JIT_AL);
  }
  if (Decoder_Rn(instr) == JIT_E_PC) {
    /* Mask out the flags into a temp reg */
    JITHostReg Rpc = JITRegAlloc_MapReg(ra, JIT_E_Temp2, 1, JIT_H_REG_NONE, JIT_NV);
    ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rd_Rn_Imm(JIT_AL,JIT_BIC,Rpc,Rn,0xFC000003));
    JITRegAlloc_LockReg(ra, Rn, -1, false);
    Rn = Rpc;
  }

  /* Calculate ALU output in a temporary register, so we can avoid write-back on abort */
  Ralu = Rn;
  if (Decoder_LDRSTR_HasOffset(instr)) {
    Ralu = JITRegAlloc_MapReg(ra, JIT_E_Temp, 1, JIT_H_REG_NONE, JIT_NV);
    if (Decoder_LDRSTR_ImmFlag(instr)) {
      codegen_bigadd(ra->block, Ralu, Rn, (instr->instr & 0xfff) * (Decoder_LDRSTR_UpFlag(instr) ? 1 : -1));
    } else {
      uint32_t offset = (instr->instr & 0xfe0) | Rm; /* Preserve shift, knock out register-shifted-register flag */
      ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rd_Rn_Rm(JIT_AL, Decoder_LDRSTR_UpFlag(instr) ? JIT_ADD : JIT_SUB, Ralu, Rn, offset));
      /* Rm no longer needed */
      JITRegAlloc_LockReg(ra, Rm, -1, false);
    }
  }

  Raddr = (Decoder_LDRSTR_PreFlag(instr) ? Ralu : Rn);

  /* Now perform load */
  if (Rd == JIT_H_REG_NONE) {
    Rd = JITRegAlloc_MapReg(ra, Decoder_Rd(instr), 1, JIT_H_REG_NONE, JIT_NV);
  }
  JITEmuInterf2_WriteLoad(ra, Rd, Raddr, Decoder_LDRSTR_ByteFlag(instr), block2->nextinstr);

  /* Write the exit handler (WriteLoad will have inserted any necessary branches) */
  {
    JITRegAlloc ra2;
    CodeGenState cgstate2;
    memcpy(&cgstate2,cgstate,sizeof(CodeGenState));
    JITRegAlloc_Copy(&ra2,ra,block2);
    codegen_sync(&ra2,&cgstate2);
    JITEmuInterf2_WriteEpilogue(&ra2,JITResult_Interpret);
  }

  /* Apply writeback */
  if (Decoder_LDRSTR_WritebackFlag(instr) && (Ralu != Rn)) {
    ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rd_Rm(JIT_AL, JIT_MOV, Rn, Ralu));
    JITRegAlloc_LockReg(ra, Rn, -1, true);
    JITRegAlloc_LockReg(ra, Ralu, -1, false);
    Rn = JIT_H_REG_NONE;
    Ralu = JIT_H_REG_NONE;
  }

  /* Release remaining regs */
  JITRegAlloc_LockReg(ra, Rd, -1, true);
  if (Ralu != Rn) {
    JITRegAlloc_LockReg(ra, Ralu, -1, false);
  }
  if (Rn != JIT_H_REG_NONE) {
    JITRegAlloc_LockReg(ra, Rn, -1, false);
  }
}

static void codegen_handle_ldr_cc(JITRegAlloc *ra1, const CodeGenState *cgstate, uint32_t cc, const Instruction *instr, ForwardCodeBlock *block2)
{
  uintptr_t branch;
  JITRegAlloc ra2;
  JITHostReg cyc;
  /* Assume cycle will be consumed */
  cyc = JITRegAlloc_MapReg(ra1, JIT_E_CycleCount, 1, JIT_H_REG_NONE, JIT_AL);
  /* Fork register state */
  JITRegAlloc_Fork(ra1,&ra2);
  /* Reserve space for branch */
  branch = ForwardCodeBlock_WriteCode(ra2.block, 0);
  /* Generate code */
  codegen_handle_ldr(&ra2,cgstate,instr,block2);
  /* Consume cycle */
  ForwardCodeBlock_WriteCode(ra2.block, JITCodeGen_DataProc_Rd_Rn_Imm(JIT_AL,JIT_ADD,cyc,cyc,1));
  /* Join state */
  JITRegAlloc_Join(ra1,&ra2);
  /* Fill in branch */
  *((uint32_t *) branch) = JITCodeGen_Branch(cc ^ (1<<28),ra2.block->nextinstr-branch);
  JITRegAlloc_LockReg(ra1, cyc, -1, true);
}

static void codegen_handle_ldm(JITRegAlloc *ra,const CodeGenState *cgstate,const Instruction *instr, ForwardCodeBlock *block2)
{
  JITHostReg Rn, Raddr;
  int regs = Decoder_LDMSTM_NumRegs(instr);
  int offset = 0;


  /* Map Rn */
  Rn = JITRegAlloc_MapReg(ra, Decoder_Rn(instr), 1, JIT_H_REG_NONE, JIT_AL);

  /* Calculate base address */
  Raddr = JITRegAlloc_MapReg(ra, JIT_E_Temp, 1, JIT_H_REG_NONE, JIT_NV);

  ForwardCodeBlock_WriteCode(ra->block, JITCodeGen_DataProc_Rd_Rn_Imm(JIT_AL,JIT_BIC,Raddr,Rn,3));
  if (Decoder_LDMSTM_PreFlag(instr)) {
    offset = 4;
  }
  if (!Decoder_LDMSTM_UpFlag(instr)) {
    offset = -offset - ((regs-1)<<2);
  }
  codegen_bigadd(ra->block, Raddr, Raddr, offset);

  /* HACK - making registers dirty in WriteLDM could break the exit handler, so write back everything here */
  JITRegAlloc_WriteBackDirty(ra);

  /* Now perform load */
  JITEmuInterf2_WriteLDM(ra, Rn, Raddr, instr, block2->nextinstr);

  /* Write the exit handler (WriteLDM will have inserted any necessary branches) */
  {
    JITRegAlloc ra2;
    CodeGenState cgstate2;
    memcpy(&cgstate2,cgstate,sizeof(CodeGenState));
    JITRegAlloc_Copy(&ra2,ra,block2);
    codegen_sync(&ra2,&cgstate2);
    JITEmuInterf2_WriteEpilogue(&ra2,JITResult_Interpret);
  }

  /* Apply writeback */
  if (Decoder_LDMSTM_WritebackFlag(instr) && !(instr->instr & (1<<Decoder_Rn(instr)))) {
    offset = (Decoder_LDMSTM_UpFlag(instr) ? 1 : -1) * (regs<<2);
    codegen_bigadd(ra->block, Rn, Rn, offset);    
    JITRegAlloc_LockReg(ra, Rn, -1, true);
  } else {
    JITRegAlloc_LockReg(ra, Rn, -1, false);
  }

  JITRegAlloc_LockReg(ra, Raddr, -1, false);
}

static void codegen_handle_ldm_cc(JITRegAlloc *ra1, const CodeGenState *cgstate, uint32_t cc, const Instruction *instr, ForwardCodeBlock *block2)
{
  uintptr_t branch;
  JITRegAlloc ra2;
  JITHostReg cyc;
  /* Assume cycles will be consumed */
  cyc = JITRegAlloc_MapReg(ra1, JIT_E_CycleCount, 1, JIT_H_REG_NONE, JIT_AL);
  /* Fork register state */
  JITRegAlloc_Fork(ra1,&ra2);
  /* Reserve space for branch */
  branch = ForwardCodeBlock_WriteCode(ra2.block, 0);
  /* Generate code */
  codegen_handle_ldm(&ra2,cgstate,instr,block2);
  /* Consume cycles */
  ForwardCodeBlock_WriteCode(ra2.block, JITCodeGen_DataProc_Rd_Rn_Imm(JIT_AL,JIT_ADD,cyc,cyc,Decoder_LDMSTM_Cycles(instr)));
  /* Join state */
  JITRegAlloc_Join(ra1,&ra2);
  /* Fill in branch */
  *((uint32_t *) branch) = JITCodeGen_Branch(cc ^ (1<<28),ra2.block->nextinstr-branch);
  JITRegAlloc_LockReg(ra1, cyc, -1, true);
}

#ifndef __riscos__
static JITResult JIT_Hack(JITEmuState *state,void *addr)
{
  return JITResult_Interpret;
}
#endif

static void JIT_GeneratePage(JITEmuState *state,JITPage *page)
{
  const JITState *jit = JIT_GetState(state);
  ForwardCodeBlock block,block2;
  void *addr = JITPage_StartOfPage(jit,page);
  int remaining = JITPAGE_SIZE;
  JITFunc *funcs = JIT_Phy2Func(jit,addr);
  CodeGenState cgstate = {false};
  bool incode = false, block2_init = false;
  JITRegAlloc ra;
#ifdef DEBUG_JIT_METRICS
  uint32_t entry_points = ~0, exit_points = -~0;
#endif
  /* Release any code that currently exists */
  JITPage_ForgetCode(jit,page);
  /* Start allocating new code */
  page->codeblock = CodeBlock_NextID();
  ForwardCodeBlock_New(&block, false);
  JITRegAlloc_Init(&ra, &block);

  while (remaining > 0)
  {
    remaining -= 4;
    if (MemAttr_GetEntryPointFlag(jit,addr))
    {
      /* Write back state */
      codegen_sync(&ra,&cgstate);
      JITRegAlloc_CalleeRestore(&ra,false);
      /* Record current location as an entry point */
#ifdef DEBUG_JIT_DUMP
      fprintf(stderr,"entry point: %08x\n",block.nextinstr);
#endif
#ifdef __riscos__
      *funcs++ = (JITFunc) block.nextinstr;
#else
      *funcs++ = &JIT_Hack;
#endif
      incode = true;
#ifdef DEBUG_JIT_METRICS
      entry_points++;
#endif
    }
    else
    {
      /* Not an entry point */
      *funcs++ = &JIT_Generate;
    }
    if (incode)
    {
      /* Decode instruction */
      Instruction instr;
      Decoder_Decode(&instr,*((uint32_t *) addr));
      if (!JITable(&instr))
      {
        /* Terminate this block */
        codegen_sync(&ra,&cgstate);
        JITEmuInterf2_WriteEpilogue(&ra,JITResult_Interpret);
        incode = false;
#ifdef DEBUG_JIT_METRICS
        exit_points++;
#endif
      }
      else
      {
        uint32_t temp,cc;
        JITHostReg Rd,Rn,Rm,Rs,Rpc;
        uintptr_t branch;
#ifdef DEBUG_JIT_DUMP
        const char *str;
        uint32_t laddr = ((state->Reg[15]-8)&0x03fff000)+JITPAGE_SIZE-4-remaining;
        _swix(Debugger_Disassemble,_INR(0,1)|_OUT(1),instr.instr,laddr,&str);
        fprintf(stderr,"%08x %08x %s\n",laddr,instr.instr,str);
#endif
        /* Sync if we've been going for a long time (hack to avoid impossible imm12) */
        if ((cgstate.cycles > 128) || (cgstate.instrs > 128)) {
          codegen_sync(&ra,&cgstate);
        }
        /* Mark this as code */
        MemAttr_SetCodeFlag(jit,addr);
#ifdef DEBUG_JIT_METRICS
        jitmetrics.instructions_in++;
#endif
        cc = Decoder_CC(&instr);
        /* Work out how to generate code */
        switch (instr.type) {
        case InstrType_NOP:
          break;

        case InstrType_DataProc:
          if (!cgstate.psr_loaded && (Decoder_Conditional(&instr) || Decoder_DataProc_SFlag(&instr) || Decoder_DataProc_CarryIn(&instr))) {
            /* PSR needed but not loaded yet */
            JITHostReg psrreg = JITRegAlloc_MapReg(&ra, JIT_E_PC, 0, JIT_H_REG_NONE, JIT_AL);
            ForwardCodeBlock_WriteCode(&block, JITCodeGen_LoadNZCV(JIT_AL,psrreg));
            cgstate.psr_loaded = true;
          }
          /* Load arguments, update instruction */
          temp = instr.instr & ~0xff000; /* Zap Rd, Rn fields */

          Rn = Rd = Rm = Rs = Rpc = JIT_H_REG_NONE;

          /* Pre-lock registers */
          if (Decoder_DataProc_IsShiftedReg(&instr)) {
            /* Add the extra cycle */
            codegen_conditional_cycles(&ra, &cgstate, cc, 1);
            Rs = JITRegAlloc_LockEmuReg(&ra, Decoder_Rs(&instr));
          }          
          if (Decoder_DataProc_UsesRn(&instr)) {
            Rn = JITRegAlloc_LockEmuReg(&ra, Decoder_Rn(&instr));
          }
          if (!Decoder_DataProc_ImmFlag(&instr)) {
            Rm = JITRegAlloc_LockEmuReg(&ra, Decoder_Rm(&instr));
          }
          if (Decoder_DataProc_UsesRd(&instr)) {
            Rd = JITRegAlloc_LockEmuReg(&ra, Decoder_Rd(&instr));
          }

          /* Now actually load them */          
          if (!Decoder_DataProc_ImmFlag(&instr)) {
            if (Rm == JIT_H_REG_NONE) {
              Rm = JITRegAlloc_MapReg(&ra, Decoder_Rm(&instr), 1, JIT_H_REG_NONE, JIT_AL);
            }
            if (Decoder_Rm(&instr) == JIT_E_PC) {
              codegen_sync_pc(&ra,&cgstate,true);
            }
            temp = (temp & ~0xf) | Rm;
          }

          if (Decoder_DataProc_UsesRn(&instr)) {
            if (Rn == JIT_H_REG_NONE) {
              Rn = JITRegAlloc_MapReg(&ra, Decoder_Rn(&instr), 1, JIT_H_REG_NONE, JIT_AL);
            }
            if (Decoder_Rn(&instr) == JIT_E_PC) {
              codegen_sync_pc(&ra,&cgstate,true);
              /* Mask out the flags into a temp reg */
              Rpc = JITRegAlloc_MapReg(&ra, JIT_E_Temp, 1, JIT_H_REG_NONE, JIT_NV);
              ForwardCodeBlock_WriteCode(&block, JITCodeGen_DataProc_Rd_Rn_Imm(cc,JIT_BIC,Rpc,Rn,0xFC000003));
              JITRegAlloc_LockReg(&ra, Rn, -1, false);
              Rn = Rpc;
            }
            temp |= (Rn<<16);
          }
          /* If Rn isn't used, leave as zero (preferred MOV/MVN encoding) */

          if (Decoder_DataProc_IsShiftedReg(&instr)) {
            if (Rs == JIT_H_REG_NONE) {
              Rs = JITRegAlloc_MapReg(&ra, Decoder_Rs(&instr), 1, JIT_H_REG_NONE, JIT_AL);
              if (Decoder_Rs(&instr) == JIT_E_PC) {
                /* Mask out the flags into a temp reg */
                Rpc = JITRegAlloc_MapReg(&ra, JIT_E_Temp2, 1, JIT_H_REG_NONE, JIT_NV);
                ForwardCodeBlock_WriteCode(&block, JITCodeGen_DataProc_Rd_Rn_Imm(cc,JIT_BIC,Rpc,Rs,0xFC000003));
                JITRegAlloc_LockReg(&ra, Rs, -1, false);
                Rs = Rpc;
              }
              /* If Rn or Rm are the PC, we also need to add 4 to them */
              if ((Rn != JIT_H_REG_NONE) && (Decoder_Rn(&instr) == JIT_E_PC)) {
                ForwardCodeBlock_WriteCode(&block, JITCodeGen_DataProc_Rd_Rn_Imm(cc,JIT_ADD,Rpc,Rn,4));                  
              }
              if ((Rm != JIT_H_REG_NONE) && (Decoder_Rm(&instr) == JIT_E_PC)) {
                /* Yuck, this will be the full PC
                   Transfer into another temp */
                Rpc = JITRegAlloc_MapReg(&ra, JIT_E_Temp3, 1, JIT_H_REG_NONE, JIT_NV);
                ForwardCodeBlock_WriteCode(&block, JITCodeGen_DataProc_Rd_Rn_Imm(cc,JIT_ADD,Rpc,Rm,4));
                JITRegAlloc_LockReg(&ra, Rm, -1, false);
                Rm = Rpc;
                temp = (temp & ~0xf) | Rm;
              }
            }
            temp = (temp & ~0xf00) | (Rs<<8);
          }          

          /* Map Rd last so we can conditionally load it based on CC */
          if (Decoder_DataProc_UsesRd(&instr)) {
            if (Rd == JIT_H_REG_NONE) {
              Rd = JITRegAlloc_MapReg(&ra, Decoder_Rd(&instr), 1, JIT_H_REG_NONE, cc ^ (1<<28));
            }
            temp |= (Rd<<12);
          }
          /* If Rd isn't used leave as zero (preferred CMP, etc. encoding)
             N.B. this will be wrong when we start supporting CMPP (need to decode them as a different InstrType?) */

          /* Perform the op */
          ForwardCodeBlock_WriteCode(&block, temp);

          /* Write back any result */
          if (Decoder_DataProc_SFlag(&instr)) {
            cgstate.psr_dirty = true;
          }
          JITRegAlloc_LockReg(&ra, Rn, -1, false);
          JITRegAlloc_LockReg(&ra, Rd, -1, true);
          JITRegAlloc_LockReg(&ra, Rm, -1, false);
          JITRegAlloc_LockReg(&ra, Rs, -1, false);
          break;

        case InstrType_Branch:
          if (!cgstate.psr_loaded && Decoder_Conditional(&instr)) {
            /* PSR needed but not loaded yet */
            JITHostReg psrreg = JITRegAlloc_MapReg(&ra, JIT_E_PC, 0, JIT_H_REG_NONE, JIT_AL);
            ForwardCodeBlock_WriteCode(&block, JITCodeGen_LoadNZCV(JIT_AL,psrreg));
            cgstate.psr_loaded = true;
          }

#ifdef DEBUG_JIT_METRICS
          exit_points++;
#endif
          /* Pre-increment these? */
          cgstate.instrs++;
          cgstate.cycles++;
          if (cc != JIT_AL) {
            /* Branch to an exit on CC */
            if (!block2_init) {
              block2_init = true;
              ForwardCodeBlock_New(&block2, false);
            }
            ForwardCodeBlock_WriteCode(&block, JITCodeGen_Branch(cc,block2.nextinstr-block.nextinstr));
            codegen_handle_branch_cc(&ra,&cgstate,&block2,&instr);
          } else {
            codegen_handle_branch(&ra,&cgstate,&instr);
            incode = false; /* Don't assume it will return */
          }
          goto next_instr;
          break;

        case InstrType_LDRSTR:
          if (!cgstate.psr_loaded && (Decoder_Conditional(&instr) || Decoder_LDRSTR_CarryIn(&instr))) {
            /* PSR needed but not loaded yet */
            JITHostReg psrreg = JITRegAlloc_MapReg(&ra, JIT_E_PC, 0, JIT_H_REG_NONE, JIT_AL);
            ForwardCodeBlock_WriteCode(&block, JITCodeGen_LoadNZCV(JIT_AL,psrreg));
            cgstate.psr_loaded = true;
          }

          if (cgstate.psr_dirty /* Force PSR writeback (fastmap will clobber it) */
          || (Decoder_Rn(&instr) == JIT_E_PC) /* Force PC write-back if needed as input */
          || (!Decoder_LDRSTR_ImmFlag(&instr) && (Decoder_Rm(&instr) == JIT_E_PC))) {
            codegen_sync_pc(&ra,&cgstate,true);
          }
          cgstate.psr_loaded = false; /* About to be clobbered */

          if (!block2_init) {
            block2_init = true;
            ForwardCodeBlock_New(&block2, false);
          }
          if (cc != JIT_AL) {
            codegen_handle_ldr_cc(&ra,&cgstate,cc,&instr,&block2);
          } else {
            codegen_handle_ldr(&ra,&cgstate,&instr,&block2);
            cgstate.cycles++;
          }
          break;

        case InstrType_LDMSTM:
          if (!cgstate.psr_loaded && Decoder_Conditional(&instr)) {
            /* PSR needed but not loaded yet */
            JITHostReg psrreg = JITRegAlloc_MapReg(&ra, JIT_E_PC, 0, JIT_H_REG_NONE, JIT_AL);
            ForwardCodeBlock_WriteCode(&block, JITCodeGen_LoadNZCV(JIT_AL,psrreg));
            cgstate.psr_loaded = true;
          }

          if (cgstate.psr_dirty) { /* Force PSR writeback (fastmap will clobber it) */
            codegen_sync_pc(&ra,&cgstate,true);
          }
          cgstate.psr_loaded = false; /* About to be clobbered */

          if (!block2_init) {
            block2_init = true;
            ForwardCodeBlock_New(&block2, false);
          }
          if (cc != JIT_AL) {
            codegen_handle_ldm_cc(&ra,&cgstate,cc,&instr,&block2);
          } else {
            codegen_handle_ldm(&ra,&cgstate,&instr,&block2);
            cgstate.cycles+=Decoder_LDMSTM_Cycles(&instr);
          }
          break;

        default:
          assert(0);
          break;
        }
        cgstate.instrs++;
        cgstate.cycles++;
next_instr:;
#ifdef DEBUG_JIT_SINGLE_INSTR
        if (incode) {
          codegen_sync(&ra,&cgstate);
          JITEmuInterf2_WriteEpilogue(&ra,JITResult_Normal);
          incode = false;
#ifdef DEBUG_JIT_METRICS
          exit_points++;
#endif
        }
#endif
      }
    }
    addr = (void *) (((uintptr_t) addr)+4);
  }
  /* If we're still in code at the end, terminate the block with Normal result */
  if (incode)
  {
    codegen_sync(&ra,&cgstate);
    JITEmuInterf2_WriteEpilogue(&ra,JITResult_Normal);
#ifdef DEBUG_JIT_METRICS
    exit_points++;
#endif
  }
  /* Finish generating code */
  CodeBlock_ClaimID(page->codeblock, page);
  DirtyRanges_Flush();
#ifdef JIT_DEBUG
  fprintf(stderr,"JIT done\n");
#endif
#ifdef DEBUG_JIT_METRICS
  if (entry_points < JITPAGE_SIZE/4) jitmetrics.entry_points[entry_points]++;
  if (exit_points < JITPAGE_SIZE/4) jitmetrics.exit_points[exit_points]++;
#endif
}

#ifdef DEBUG_JIT_TEST_EXEC
#include "../armemu.h"
extern void extern_execute_instruction(ARMul_State *state,ARMword instr,ARMword r15);

extern JITResult test_exec(JITEmuState *state,ARMword *addr);

JITResult test_exec(JITEmuState *state,ARMword *addr)
{
  /* Make a backup of the state */
  const JITState *jit = JIT_GetState(state);
#ifndef DEBUG_JIT_FAKE
  ARMword r[16],r2[16];
  CycleCount cyc = state->NumCycles;
#endif
  JITResult res;
  JITPage *page = JITPage_Get(jit,addr);
  ARMword *page_end = (ARMword *) JITPage_StartOfPage(jit,page);
  ARMword *initial = addr;
  JITFunc *func = JIT_Phy2Func(jit,addr);
  page_end += JITPAGE_SIZE/4;
#ifndef DEBUG_JIT_FAKE
  memcpy(r,state->Reg,sizeof(r));
#endif
  /* Single-step over all code */
  while ((addr != page_end) && MemAttr_GetCodeFlag(jit,addr)) {
    ARMword oldr15 = state->Reg[15];
    /* Stop here for any taken load instruction */
    {
      Instruction instr;
      Decoder_Decode(&instr, *addr);
      if ((instr.type == InstrType_LDRSTR) && ARMul_CCCheck(instr.instr,(oldr15 & CCBITS))) {
        break;
      }
    }
    state->NextInstr = NORMAL;
    state->NumCycles++;
    extern_execute_instruction(state,*addr,state->Reg[15]);
    if (state->NextInstr == NORMAL)
    {
      if ((oldr15 & 0xfffffff) != (state->Reg[15] & 0xfffffff))
      {
        fprintf(stderr,"Unexpected PC modification %08x -> %08x @ %08x\n",oldr15,state->Reg[15],*addr);
      }
      state->Reg[15]+=4;
    }
    else if (state->NextInstr != PCINCED)
    {
      fprintf(stderr,"Unexpected pipeline state %d @ %08x\n",state->NextInstr,*addr);
    }
    addr++;
#ifdef DEBUG_JIT_SINGLE_INSTR
    break;
#endif
  }
#ifndef DEBUG_JIT_FAKE
  /* Copy state again */
  memcpy(r2,state->Reg,sizeof(r));
  /* Restore original state */
  memcpy(state->Reg,r,sizeof(r));
  state->NumCycles = cyc;
  /* Run the code */
  res = (*func)(state,initial);
  /* Check for consistency */
  if (memcmp(state->Reg,r2,sizeof(r))) {
    int i;
    fprintf(stderr,"JIT inconsistency!\n");
    fprintf(stderr,"Orig:");
    for(i=0;i<16;i++)
    {
      fprintf(stderr," %08x",r[i]);
    }
    fprintf(stderr,"\nStep:");
    for(i=0;i<16;i++)
    {
      fprintf(stderr," %08x",r2[i]);
    }
    fprintf(stderr,"\nJIT :");
    for(i=0;i<16;i++)
    {
      fprintf(stderr," %08x",state->Reg[i]);
    }
    fprintf(stderr,"\nCode:\n");
    for(i=0;initial+i != (addr+1);i++) {
      char *str;
      _swix(Debugger_Disassemble,_INR(0,1)|_OUT(1),initial[i],(r[15]&0x03fffffc)-8+(i*4),&str);
      fprintf(stderr,"%08x %08x %s\n",(r[15]&0x03fffffc)-8+(i*4),initial[i],str);
    }
#if 0 /* Broken, MOV PC, R14 is no longer the only terminating instruction (plus there could be branches) */
    fprintf(stderr,"\nJIT:\n");
    addr = (ARMword *) *func;
    do {
      char *str;
      _swix(Debugger_Disassemble,_INR(0,1)|_OUT(1),*addr,addr,&str);
      fprintf(stderr,"%08x %08x %s\n",addr,*addr,str);
    } while (*(addr++) != 0xe1a0f00e);
#else
    fprintf(stderr,"\nJIT: %08x\n",*func);
#endif
    assert(0);
  }
  return res;
#else
  state->NextInstr = (addr==page_end ? PCINCED : NORMAL);
  return (addr==page_end ? JITResult_Normal : JITResult_Interpret);
#endif
}
#endif

JITResult JIT_Generate(JITEmuState *state,void *addr)
{
  const JITState *jit = JIT_GetState(state);
  Instruction instr;
  uint32_t *input = (uint32_t *) addr;
  Decoder_Decode(&instr,*input);
  if (!JITable(&instr))
  {
    return JITResult_Interpret;
  }
#ifdef DEBUG_JIT_METRICS
  jitmetrics.generate_count++;
#endif
  /* Flag this location as being an entry point */
  MemAttr_SetEntryPointFlag(jit,addr);
  /* Generate all the code for this page */
  JIT_GeneratePage(state,JITPage_Get(jit,addr));
  /* Re-fetch and execute the JIT function (mustn't be JIT_Generate!) */
#ifdef DEBUG_JIT_TEST_EXEC
  return test_exec(state,(ARMword *) addr);
#else
  return (*JIT_Phy2Func(jit,addr))(state,addr);
#endif
}
