/* Manually include jitstate2.h first (necessary for everything to be defined in the right order with ArcEm) */
#include "jitstate2.h"

#ifndef JIT_HEADER
#define JIT_HEADER

#include "memattr.h"
#include "jitpage.h"

/* Initialise the JIT
   * romramchunk must be a pointer to the memory block containing all ROM/RAM/VRAM etc.
     Each sub-section must start at a 4K offset into the block
   * romramchunksize must be the size of romramchunk (4K multiple)
 */
extern void JIT_Init(JITState *jit,uintptr_t romramchunk,uint32_t romramchunksize);

/* Try to generate (and execute) code for the given address */
extern JITResult JIT_Generate(JITEmuState *state,void *addr);

/* Return location of JIT function associated with addr
   addr must be word aligned! */
static inline JITFunc *JIT_Phy2Func(const JITState *jit,void *addr)
{
  return (JITFunc *) (((uintptr_t)addr) + jit->addr2func);
}

/* Clobber any generated code at the given address */
static inline void JIT_ClobberCode(JITEmuState *state,void *addr)
{
  JITState *jit = JIT_GetState(state);  
  if (!MemAttr_GetCodeFlag(jit,addr))
  {
    return;
  }
  JITPage_ClobberCodeByAddr(jit,addr);
}

#endif
