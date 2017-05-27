#include "dirtyranges.h"
#include "metrics.h"
#include <assert.h>
#include <stdio.h>

#ifdef __riscos__
#include <kernel.h>
#include <swis.h>
#endif

#define MAX_DIRTY_RANGES 256

typedef struct {
  DirtyRange ranges[MAX_DIRTY_RANGES];
  int index;
} DirtyRanges;

static DirtyRanges dirty;

DirtyRange *DirtyRanges_Claim(uintptr_t addr)
{
  assert(dirty.index < MAX_DIRTY_RANGES);
  DirtyRange *r = &dirty.ranges[dirty.index++];
  r->start = r->end = addr;
  return r;
}

void DirtyRanges_Flush(void)
{
#ifdef DEBUG_JIT_DUMP
  uint32_t *addr;
  fprintf(stderr,"JIT:\n");
#endif
  /* TODO optimise (can IMB_List be called in user mode?) */
  while (dirty.index > 0)
  {
    DirtyRange *r = &dirty.ranges[--dirty.index];
#ifdef DEBUG_JIT_METRICS
    jitmetrics.instructions_out += (r->end - r->start)>>2;
#endif
#ifdef DEBUG_JIT_DUMP
    for(addr=(uint32_t *)r->start;addr!=(uint32_t *)r->end;addr++) {        
      char *str;
      _swix(Debugger_Disassemble,_INR(0,1)|_OUT(1),*addr,addr,&str);
      fprintf(stderr,"%08x %08x %s\n",addr,*addr,str);
    }
#endif
#ifdef __riscos__
    if (r->start != r->end) {
      _swix(OS_SynchroniseCodeAreas,_INR(0,2),1,r->start,r->end-4);
    }
#endif
  }
}
