#ifndef MEMATTR_HEADER
#define MEMATTR_HEADER

#include "jitstate.h"
#include <string.h>

#define JIT_MEMFLAG_CODE 1
#define JIT_MEMFLAG_ENTRYPOINT 2

extern void MemAttr_Init(JITState *jit,uint32_t romramchunksize);

static inline uint8_t *MemAttr_Get(const JITState *jit,void *phy)
{
  return (uint8_t *) (jit->addr2flags + (((uintptr_t) phy)>>2));
}

static inline void MemAttr_SetCodeFlag(const JITState *jit,void *phy)
{
  *MemAttr_Get(jit,phy) |= JIT_MEMFLAG_CODE;
}

static inline void MemAttr_SetEntryPointFlag(const JITState *jit,void *phy)
{
  *MemAttr_Get(jit,phy) |= JIT_MEMFLAG_ENTRYPOINT;
}

static inline void MemAttr_ClearFlags(const JITState *jit,void *phy)
{
  *MemAttr_Get(jit,phy) = 0;
}

static inline void MemAttr_ClearFlagsRange(const JITState *jit,void *phy_begin, void *phy_end)
{
  uint8_t *begin = MemAttr_Get(jit,phy_begin);
  uint8_t *end = MemAttr_Get(jit,phy_end);
  memset(begin, 0, end-begin);
}

static inline bool MemAttr_GetCodeFlag(const JITState *jit,void *phy)
{
  return (*MemAttr_Get(jit,phy)) & JIT_MEMFLAG_CODE;
}

static inline bool MemAttr_GetEntryPointFlag(const JITState *jit,void *phy)
{
  return (*MemAttr_Get(jit,phy)) & JIT_MEMFLAG_ENTRYPOINT;  
}

#endif
