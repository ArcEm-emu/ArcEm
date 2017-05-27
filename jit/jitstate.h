#ifndef JITSTATE_HEADER
#define JITSTATE_HEADER

#include "emuinterf.h"

typedef enum {
  JITResult_Interpret, /* Interpret the instruction at PC-8 */
  JITResult_Normal, /* Continue normal execution (try JITing the instruction at the PC-8) */
} JITResult;

typedef JITResult (*JITFunc)(JITEmuState *state,void *addr);

typedef struct JITPage JITPage;

/* Private JIT state struct */
typedef struct {
  uintptr_t addr2func; /* Offset to apply to address pointers to convert to func pointers */
  uintptr_t addr2flags; /* Offset to apply to convert (shifted) address pointers to memory flag pointers */

  JITFunc *phy2func;
  uint8_t *memflags;
  JITPage *pages;
  uintptr_t romramchunk;
#ifdef DEBUG_JIT_METRICS_EXEC
  uint32_t exec_count;
#endif
} JITState;

#endif
