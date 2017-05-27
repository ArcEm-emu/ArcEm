/* Manually include armdefs.h first (necessary for everything to be defined in the right order with ArcEm) */
#include "../armdefs.h"

#ifndef JITSTATE2_HEADER
#define JITSTATE2_HEADER

/* Accessor function to get JITState from JITEmuState */
static inline JITState *JIT_GetState(JITEmuState *state)
{
  return &state->jit;
}

#endif
