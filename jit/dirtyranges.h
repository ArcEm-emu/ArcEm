#ifndef DIRTYRANGES_HEADER
#define DIRTYRANGES_HEADER

#include "emuinterf.h"

typedef struct {
  uintptr_t start;
  uintptr_t end;
} DirtyRange;

extern DirtyRange *DirtyRanges_Claim(uintptr_t addr);

extern void DirtyRanges_Flush(void);

#endif
