#ifndef REGALLOC_HEADER
#define REGALLOC_HEADER

#include "emuinterf.h"
#include "codeblocks.h" /* YUCK */

typedef enum {
  JIT_H_R0 = 0,
  /* JIT_H_R1 ... JIT_H_R15 as expected */
  JIT_H_PC = 15,
  JIT_H_PSR = 16,
  JIT_H_REG_NUM,
  JIT_H_REG_NONE = -1
} JITHostReg;

typedef enum {
  JIT_E_R0 = 0,
  /* JIT_E_R1 ... JIT_E_R15 as expected */
  JIT_E_PC = 15,
  JIT_E_CycleCount,
  JIT_E_Temp,
  JIT_E_Temp2,
  JIT_E_Temp3,
  JIT_E_Temp4,
  JIT_E_StateReg,
#ifdef DEBUG_JIT_METRICS_EXEC
  JIT_E_Exec,
#endif
  JIT_E_REG_NUM,
  JIT_E_REG_NONE = -1
} JITEmuReg;

#define JIT_HR(x) ((JITHostReg) (JIT_H_R0 + (x)))
#define JIT_ER(x) ((JITEmuReg) (JIT_E_R0 + (x)))

typedef struct {
  JITHostReg hostreg; /* JIT_H_REG_NONE if not loaded */
} JITEmuRegState;

typedef struct {
  JITEmuReg emureg; /* JIT_E_REG_NONE if not in use */
  bool dirty; /* true if value needs writing back */
  uint8_t required; /* >0 if required */
} JITHostRegState;

typedef struct {
  JITEmuRegState emu_regs[JIT_E_REG_NUM]; /* Which host reg an emu reg maps to */
  JITHostRegState host_regs[JIT_H_REG_NUM]; /* Which emu reg a host reg maps to */
  uint32_t free_host_regs; /* Host registers which are free for immediate use */
  uint32_t callee_save; /* Host registers which can be made free for use by callee-save mechanism */
  int next_idx;
  uint32_t *callee_save_instr; /* STR/STM that implements the callee-save */
  uint32_t callee_saved; /* Which callee-save regs have been saved */
  ForwardCodeBlock *block; /* Code block to use for writing any instructions */
} JITRegAlloc;


/* Initialise a register allocator */
extern void JITRegAlloc_Init(JITRegAlloc *ra, ForwardCodeBlock *block);

/* Copy a register allocator, changing the associated code block
   Use when permanently splitting the path of execution */
extern void JITRegAlloc_Copy(JITRegAlloc *dest, JITRegAlloc *src, ForwardCodeBlock *block);

/* Copy a register allocator
   Use when temporarily splitting the path of execution
   Parent must not be used until joined with child */
extern void JITRegAlloc_Fork(JITRegAlloc *parent, JITRegAlloc *child);

/* Join the two paths so only parent remains */
extern void JITRegAlloc_Join(JITRegAlloc *parent, JITRegAlloc *child);

/* Trigger callee-saving of hr */
extern void JITRegAlloc_CalleeSave(JITRegAlloc *ra, JITHostReg hr);

/* Request that r is made available
   Specify hostreg of JIT_H_REG_NONE for automatic register assignment
   required should be 1 or 0
   cc specifies the condition under which the value should be loaded (JIT_NV if we always overwrite) */
extern JITHostReg JITRegAlloc_MapReg(JITRegAlloc *ra, JITEmuReg r, int required, JITHostReg hostreg, uint32_t cc);

/* Lock/unlock a register by incrementing/decrementing the required count */
static inline void JITRegAlloc_LockReg(JITRegAlloc *ra, JITHostReg hr, int required, bool dirty)
{
  if (hr != JIT_H_REG_NONE)
  {
    ra->host_regs[hr].required += required;
    ra->host_regs[hr].dirty |= dirty;
  }
}

/* Unmap a given emu register */
extern void JITRegAlloc_UnmapReg(JITRegAlloc *ra, JITEmuReg r);

/* Return a free host reg */
extern JITHostReg JITRegAlloc_GetFreeHostReg(JITRegAlloc *ra);

/* Unmap a given host register */
static inline void JITRegAlloc_FlushHostReg(JITRegAlloc *ra, JITHostReg hostreg)
{
  JITRegAlloc_UnmapReg(ra, ra->host_regs[hostreg].emureg);
}

/* Unmap all registers */
extern void JITRegAlloc_UnmapAll(JITRegAlloc *ra);

/* Lock and return an emu reg if it's present */
static inline JITHostReg JITRegAlloc_LockEmuReg(JITRegAlloc *ra, JITEmuReg r)
{
  JITHostReg hr = ra->emu_regs[r].hostreg;
  if (hr != JIT_H_REG_NONE)
  {
    ra->host_regs[hr].required++;
  }
  return hr;
}

/* Write back host reg if dirty (but keep resident) */
extern void JITRegAlloc_WriteBack(JITRegAlloc *ra, JITHostReg hr);

/* Write back all dirty, non-required regs (but keep resident) */
extern void JITRegAlloc_WriteBackUnrequired(JITRegAlloc *ra);

/* Write back all dirty regs (but keep resident) */
extern void JITRegAlloc_WriteBackDirty(JITRegAlloc *ra);

/* Restore callee-save regs, and/or return from function. Resets state. */
extern void JITRegAlloc_CalleeRestore(JITRegAlloc *ra, bool ret);

#endif
