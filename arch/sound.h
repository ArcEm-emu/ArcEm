#ifndef SOUND_H
#define SOUND_H

#include "../c99.h"

typedef int16_t SoundData;

extern int Sound_BatchSize; /* How many 16*2 sample batches to attempt to deliver to the platform code at once */
extern CycleCount Sound_DMARate; /* How many cycles between DMA fetches */
extern CycleDiff Sound_FudgeRate; /* Extra fudge factor applied to Sound_DMARate */

typedef enum {
  Stereo_LeftRight, /* Data is ordered with left channel first */
  Stereo_RightLeft, /* Data is ordered with right channel first */
} Sound_StereoSense;

extern Sound_StereoSense eSound_StereoSense;

extern int Sound_Init(ARMul_State *state);

extern void Sound_UpdateDMARate(ARMul_State *state);

#ifdef SOUND_SUPPORT
extern uint32_t Sound_HostRate; /* Rate of host sound system, in 1/1024 Hz. Must be set by host on init. */

/* These calls are made by DispKbdShared when the corresponding registers are updated */
extern void Sound_SoundFreqUpdated(ARMul_State *state);
extern void Sound_StereoUpdated(ARMul_State *state);

/* This call is made to the platform code upon initialisation */
extern int Sound_InitHost(ARMul_State *state);

/* This call is made to the platform code to get a pointer to an output buffer
   destavail must be set to the available space, measured in the number of stereo pairs (i.e. 4 byte units)
*/
extern SoundData *Sound_GetHostBuffer(int32_t *destavail);

/* This call is made to the platform code once the above buffer has been filled
   numSamples is the number of stereo pairs that were placed in the buffer
*/
extern void Sound_HostBuffered(SoundData *buffer,int32_t numSamples);
#endif

#endif
