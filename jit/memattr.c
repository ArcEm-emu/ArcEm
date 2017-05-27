#include <stdlib.h>
#include "memattr.h"

void MemAttr_Init(JITState *jit,uint32_t romramchunksize)
{
  jit->memflags = calloc(romramchunksize>>2,1);
  jit->addr2flags = ((uintptr_t) jit->memflags)-(jit->romramchunk>>2);
}
