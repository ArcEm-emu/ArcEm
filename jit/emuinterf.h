#ifndef JITEMUINTERF_HEADER
#define JITEMUINTERF_HEADER

/* Interface from the JIT to the main emulator
   Emulator-specific stuff goes here! */

#include "../c99.h"

/* Type used by emulator to store its state */
typedef struct ARMul_State JITEmuState;

/* Return pointer to global state object */
extern JITEmuState *JITEmuInterf_GetState(void);

#endif
