/* The hard drive controller as used in the A4x0 and on the podule for
   A3x0's.

   (C) David Alan Gilbert 1995
*/

#ifndef HDC63463_HEADER
#define HDC63463_HEADER

#include "../armdefs.h"

/* Structure used to store the configurable shape of the Harddrive */
struct HDCshape {
  uint32_t NCyls,NHeads,NSectors,RecordLength;
};

/* Write to HDC memory space */
void HDC_Write(ARMul_State *state, int offset, int data);
/* Read from HDC memory space */
ARMword HDC_Read(ARMul_State *state, int offset);

void HDC_Init(ARMul_State *state);

unsigned int HDC_Regular(ARMul_State *state);

#endif
